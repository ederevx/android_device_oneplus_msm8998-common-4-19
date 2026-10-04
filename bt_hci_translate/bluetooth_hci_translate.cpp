/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Userspace HCI translation layer for the QTI SLIMbus A2DP offload.
//
// The Android Bluetooth stack's AOSP offload start (HCI opcode 0xFD5D)
// describes an ACL/L2CAP transport, but this platform's ADSP hands the encoded
// stream to the controller over SLIMbus. The QTI command set instead configures
// the transport explicitly (HCI opcode 0xFC0A). This layer wraps the vendor
// HIDL IBluetoothHci implementation: it intercepts the 0xFD5D offload command,
// emits the QTI 0xFC0A sequence to the real controller, consumes the QTI
// command-complete events, and synthesises the 0xFD5D completion the stack
// waits for. Every other HCI command/event passes through untouched.
//
// An intercepted command is ALWAYS completed: the stop is completed immediately
// after the QTI stop is queued, and the start chain is guarded by a bounded
// watchdog that synthesises a failure if the QTI completion never arrives. This
// is required because the stack times out and aborts on an uncompleted command.
//
// The vendor interface is declared at v1.0 on this device, so this layer links
// and implements only android.hardware.bluetooth@1.0.
//
// The stock implementation is installed beside this one, outside the HIDL HAL
// search directory, and is loaded by absolute path.
//
// Properties:
//   persist.bluetooth.a2dp_offload.qti=0        disable the translation
//   persist.bluetooth.a2dp_offload.qti.multi=1  force the full QTI multi-VSC chain
//   persist.bluetooth.a2dp_offload.qti.slim_port / .slim_ch  SLIMbus port/channel for 0x07

#define LOG_TAG "bt_hci_translate"

#include <android/hardware/bluetooth/1.0/IBluetoothHci.h>
#include <android/hardware/bluetooth/1.0/IBluetoothHciCallbacks.h>
#include <android/hardware/bluetooth/1.0/types.h>
#include <dlfcn.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <hidl/HidlSupport.h>
#include <hidl/Status.h>
#include <log/log.h>
#include <mutex>
#include <sys/system_properties.h>
#include <thread>
#include <vector>

using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;

using IBluetoothHci_1_0 = ::android::hardware::bluetooth::V1_0::IBluetoothHci;
using IBluetoothHciCallbacks_1_0 = ::android::hardware::bluetooth::V1_0::IBluetoothHciCallbacks;
using HidlStatus = ::android::hardware::bluetooth::V1_0::Status;

namespace {

constexpr uint16_t kAospA2dpOpcode = 0xFD5D;  // HCI_CONTROLLER_A2DP
constexpr uint16_t kQtiA2dpOpcode = 0xFC0A;   // HCI_VSQC_CONTROLLER_A2DP_OPCODE

constexpr uint8_t kAospOffloadStart = 0x01;
constexpr uint8_t kAospOffloadStartV2 = 0x03;
constexpr uint8_t kAospOffloadStop = 0x02;
constexpr uint8_t kAospOffloadStopV2 = 0x04;

constexpr uint8_t kQtiWriteMediaChannelCfg = 0x03;
constexpr uint8_t kQtiStartA2dpMedia = 0x04;
constexpr uint8_t kQtiStopA2dpMedia = 0x05;
constexpr uint8_t kQtiTransportConfig = 0x07;
constexpr uint8_t kQtiWriteScmsCp = 0x08;
constexpr uint8_t kQtiSelectedCodec = 0x09;
constexpr uint8_t kQtiOffloadStart = 0x0A;

constexpr uint8_t kEvtCommandComplete = 0x0E;
constexpr uint8_t kEvtCommandStatus = 0x0F;

constexpr uint8_t kTransportSlimbus = 0x00;
constexpr uint8_t kInvalidParams = 0x12;

constexpr int kCompletionTimeoutMs = 2000;

int32_t prop_int(const char* key, int32_t fallback) {
  char value[PROP_VALUE_MAX] = {};
  if (__system_property_get(key, value) > 0) {
    return atoi(value);
  }
  return fallback;
}

bool translation_enabled() {
  char value[PROP_VALUE_MAX] = {};
  if (__system_property_get("persist.bluetooth.a2dp_offload.qti", value) > 0) {
    return value[0] != '0';
  }
  return true;
}

// Force the full QTI multi-VSC chain even if the single VSC succeeds, so the
// 0x07 A2DP_TRANSPORT_CONFIGURATION (with the SLIMbus port/channel) always goes
// out. Default off keeps the single-VSC A/B path.
bool translation_multi() {
  return prop_int("persist.bluetooth.a2dp_offload.qti.multi", 0) != 0;
}

// Diagnostic: after the chain completes, issue READ_A2DP_CFG (0xFC0A subop 0x01)
// and log the reply, in case the controller reports the SLIMbus port it expects.
bool read_cfg_enabled() {
  return prop_int("persist.bluetooth.a2dp_offload.qti.readcfg", 0) != 0;
}

// QTI codec ids: SBC 0, AAC 2, LDAC 4, aptX 8, aptX-HD 9.
uint8_t qti_codec_type(uint32_t aosp_codec) {
  switch (aosp_codec) {
    case 0x01:
      return 0;  // SBC
    case 0x02:
      return 2;  // AAC
    case 0x10:
      return 4;  // LDAC
    case 0x04:
      return 8;  // aptX
    case 0x08:
      return 9;  // aptX-HD
    default:
      return 0xFF;
  }
}

class TranslateHci;

class ProxyCallbacks : public IBluetoothHciCallbacks_1_0 {
 public:
  explicit ProxyCallbacks(TranslateHci* owner) : owner_(owner) {}
  Return<void> initializationComplete(HidlStatus status) override;
  Return<void> hciEventReceived(const hidl_vec<uint8_t>& packet) override;
  Return<void> aclDataReceived(const hidl_vec<uint8_t>& packet) override;
  Return<void> scoDataReceived(const hidl_vec<uint8_t>& packet) override;

 private:
  TranslateHci* owner_;
};

class TranslateHci : public IBluetoothHci_1_0 {
 public:
  explicit TranslateHci(sp<IBluetoothHci_1_0> real) : real_(std::move(real)) {}

  Return<void> initialize(const sp<IBluetoothHciCallbacks_1_0>& callback) override {
    ALOGE("bt_hci_translate: initialize()");
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    stack_cb_ = callback;
    proxy_cb_ = new ProxyCallbacks(this);
    return real_->initialize(proxy_cb_);
  }

  Return<void> sendHciCommand(const hidl_vec<uint8_t>& command) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    size_t n = command.size();
    const uint8_t* p = command.data();
    ALOGE("bt_hci_translate: tx len=%zu head=%02x %02x %02x %02x %02x %02x %02x %02x", n,
          n > 0 ? p[0] : 0, n > 1 ? p[1] : 0, n > 2 ? p[2] : 0, n > 3 ? p[3] : 0, n > 4 ? p[4] : 0,
          n > 5 ? p[5] : 0, n > 6 ? p[6] : 0, n > 7 ? p[7] : 0);

    // The HIDL HciPacket carries the command without the H4 type octet; accept a
    // leading 0x01 type octet too, so the intercept does not depend on framing.
    size_t base = 0;
    if (n >= 4 && p[0] == 0x01 && p[1] == static_cast<uint8_t>(kAospA2dpOpcode & 0xFF) &&
        p[2] == static_cast<uint8_t>(kAospA2dpOpcode >> 8)) {
      base = 1;
    }
    if (n >= base + 4 && static_cast<uint16_t>(p[base] | (p[base + 1] << 8)) == kAospA2dpOpcode) {
      // Params begin after the 2-byte opcode and 1-byte length.
      uint8_t sub = p[base + 3];
      uint32_t codec = n >= base + 8 ? static_cast<uint32_t>(p[base + 4] | (p[base + 5] << 8) |
                                                             (p[base + 6] << 16) |
                                                             (p[base + 7] << 24))
                                     : 0;
      ALOGE("bt_hci_translate: 0xFD5D subop=0x%02x codec=0x%08x -> %s", sub, codec,
            (sub == kAospOffloadStart || sub == kAospOffloadStartV2 || sub == kAospOffloadStop ||
             sub == kAospOffloadStopV2)
                    ? "translate"
                    : "pass-through");
      if (sub == kAospOffloadStart || sub == kAospOffloadStartV2) {
        start_offload_locked(command, base + 3);
        return Void();
      }
      if (sub == kAospOffloadStop || sub == kAospOffloadStopV2) {
        // The stop is always completed, whatever the QTI side does: queue the
        // QTI stop, then immediately complete the AOSP command. Leaving it
        // uncompleted times out the stack's HCI command and kills the HAL.
        ALOGE("bt_hci_translate: stop -> QTI 0x05, completing AOSP immediately");
        send_qti_locked({kQtiStopA2dpMedia, 0x00});
        gen_++;
        step_ = Step::Idle;
        synth_aosp_cc_locked(0, kAospOffloadStop);
        return Void();
      }
    }
    return real_->sendHciCommand(command);
  }

  Return<void> sendAclData(const hidl_vec<uint8_t>& data) override {
    return real_->sendAclData(data);
  }

  Return<void> sendScoData(const hidl_vec<uint8_t>& data) override {
    return real_->sendScoData(data);
  }

  Return<void> close() override { return real_->close(); }

  // ---- called by ProxyCallbacks -------------------------------------------
  void forward_initialization(HidlStatus status) {
    ALOGE("bt_hci_translate: initializationComplete status=%d", static_cast<int>(status));
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (stack_cb_ != nullptr) {
      stack_cb_->initializationComplete(status);
    }
  }

  void forward_event(const hidl_vec<uint8_t>& event) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    size_t n = event.size();
    const uint8_t* p = event.data();
    if (n > 0 && p[0] == 0xFF) {
      // Vendor event: log the whole payload; these carry the controller's
      // asynchronous answers and must not be silently dropped.
      char hex[3 * 64 + 1];
      size_t pos = 0;
      for (size_t i = 0; i < n && i < 64; ++i) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x ", p[i]);
      }
      ALOGE("bt_hci_translate: vendor event 0xFF hex=[%s]", hex);
    } else if (step_ != Step::Idle) {
      ALOGE("bt_hci_translate: rx len=%zu head=%02x %02x %02x %02x %02x %02x %02x %02x step=%d", n,
            n > 0 ? p[0] : 0, n > 1 ? p[1] : 0, n > 2 ? p[2] : 0, n > 3 ? p[3] : 0,
            n > 4 ? p[4] : 0, n > 5 ? p[5] : 0, n > 6 ? p[6] : 0, n > 7 ? p[7] : 0,
            static_cast<int>(step_));
    }
    uint8_t status = 0;
    uint8_t subop = 0;
    uint8_t evt_type = 0;
    if (is_qti_event(event, &status, &subop, &evt_type)) {
      char hex[3 * 64 + 1];
      size_t pos = 0;
      for (size_t i = 0; i < n && i < 64; ++i) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x ", p[i]);
      }
      ALOGE("bt_hci_translate: QTI event hex=[%s]", hex);
      if (evt_type == kEvtCommandComplete && subop == 0x01) {
        char params[3 * 64 + 1];
        size_t q = 0;
        for (size_t i = 7; i < n && i < 64; ++i) {
          q += snprintf(params + q, sizeof(params) - q, "%02x ", p[i]);
        }
        ALOGE("bt_hci_translate: READ_A2DP_CFG reply status=0x%02x return_params=[%s]", status,
              params);
      }
      if (step_ == Step::Idle) {
        ALOGE("bt_hci_translate: stray QTI event ignored (type=0x%02x status=0x%02x)", evt_type,
              status);
      } else {
        on_qti_cc_locked(status, subop, evt_type);
      }
      return;
    }
    deliver_event_locked(event);
  }

  void forward_acl(const hidl_vec<uint8_t>& data) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (stack_cb_ != nullptr) {
      stack_cb_->aclDataReceived(data);
    }
  }

  void forward_sco(const hidl_vec<uint8_t>& data) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (stack_cb_ != nullptr) {
      stack_cb_->scoDataReceived(data);
    }
  }

 private:
  enum class Step {
    Idle,
    AwaitSingleStart,
    AwaitSelectCodec,
    AwaitTransportConfig,
    AwaitMediaChannel,
    AwaitStartMedia,
  };

  // Match a QTI command completion or command status for opcode 0xFC0A.
  static bool is_qti_event(const hidl_vec<uint8_t>& event, uint8_t* status, uint8_t* subop,
                           uint8_t* evt_type) {
    if (event.size() < 4) {
      return false;
    }
    if (event[0] == kEvtCommandComplete && event.size() >= 7) {
      uint16_t opcode = static_cast<uint16_t>(event[3] | (event[4] << 8));
      if (opcode != kQtiA2dpOpcode) {
        return false;
      }
      *status = event[5];
      *subop = event[6];
      *evt_type = kEvtCommandComplete;
      return true;
    }
    if (event[0] == kEvtCommandStatus && event.size() >= 6) {
      uint16_t opcode = static_cast<uint16_t>(event[4] | (event[5] << 8));
      if (opcode != kQtiA2dpOpcode) {
        return false;
      }
      *status = event[2];
      *subop = 0;
      *evt_type = kEvtCommandStatus;
      return true;
    }
    return false;
  }

  void deliver_event_locked(const hidl_vec<uint8_t>& event) {
    if (stack_cb_ != nullptr) {
      stack_cb_->hciEventReceived(event);
    }
  }

  void send_qti_locked(const std::vector<uint8_t>& params) {
    char hex[3 * 64 + 1];
    size_t pos = 0;
    for (size_t i = 0; i < params.size() && i < 64; ++i) {
      pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x ", params[i]);
    }
    hidl_vec<uint8_t> command(params.size() + 3);
    command[0] = static_cast<uint8_t>(kQtiA2dpOpcode & 0xFF);
    command[1] = static_cast<uint8_t>((kQtiA2dpOpcode >> 8) & 0xFF);
    command[2] = static_cast<uint8_t>(params.size());
    for (size_t i = 0; i < params.size(); ++i) {
      command[3 + i] = params[i];
    }
    ALOGE("bt_hci_translate: sending QTI 0xFC0A hex=[%s] len=%zu", hex, params.size());
    auto ret = real_->sendHciCommand(command);
    if (!ret.isOk()) {
      ALOGE("bt_hci_translate: QTI send failed: %s", ret.description().c_str());
    }
  }

  void synth_aosp_cc_locked(uint8_t status, uint8_t subop) {
    hidl_vec<uint8_t> event(7);
    event[0] = kEvtCommandComplete;
    event[1] = 0x05;  // num_hci_cmds(1) + opcode(2) + return params(2)
    event[2] = 0x01;
    event[3] = static_cast<uint8_t>(kAospA2dpOpcode & 0xFF);
    event[4] = static_cast<uint8_t>((kAospA2dpOpcode >> 8) & 0xFF);
    event[5] = status;
    event[6] = subop;
    ALOGE("bt_hci_translate: synthesising 0xFD5D completion status=0x%02x subop=0x%02x", status,
          subop);
    deliver_event_locked(event);
  }

  // Guarantees an intercepted start command is completed even if no QTI
  // completion ever arrives, so the stack cannot time out and abort.
  void arm_watchdog(const uint8_t aosp_subop) {
    uint64_t gen = gen_++;
    std::thread([this, gen, aosp_subop]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(kCompletionTimeoutMs));
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      if (gen_.load() != gen + 1 || step_ == Step::Idle) {
        return;  // already completed or superseded
      }
      ALOGE("bt_hci_translate: watchdog fired, no QTI completion; completing AOSP");
      gen_++;
      step_ = Step::Idle;
      synth_aosp_cc_locked(kInvalidParams, aosp_subop);
    }).detach();
  }

  void start_offload_locked(const hidl_vec<uint8_t>& command, size_t poff) {
    if (command.size() < poff + 25) {
      ALOGE("bt_hci_translate: start payload too short (%zu, need %zu)", command.size(),
            poff + 25);
      synth_aosp_cc_locked(kInvalidParams, kAospOffloadStart);
      return;
    }
    const uint8_t* c = command.data() + poff;
    codec_type_ =
            qti_codec_type(static_cast<uint32_t>(c[1] | (c[2] << 8) | (c[3] << 16) | (c[4] << 24)));
    if (codec_type_ == 0xFF) {
      ALOGE("bt_hci_translate: unsupported aosp codec 0x%08x",
            static_cast<uint32_t>(c[1] | (c[2] << 8) | (c[3] << 16) | (c[4] << 24)));
      synth_aosp_cc_locked(kInvalidParams, kAospOffloadStart);
      return;
    }
    sample_rate_ = static_cast<uint16_t>(c[9] | (c[10] << 8));
    cp_active_ = c[7];
    cp_flag_ = c[8];
    acl_hdl_ = static_cast<uint16_t>(c[19] | (c[20] << 8));
    l2c_rcid_ = static_cast<uint16_t>(c[21] | (c[22] << 8));
    mtu_ = static_cast<uint16_t>(c[23] | (c[24] << 8));
    for (size_t i = 0; i < sizeof(codec_info_); ++i) {
      codec_info_[i] = command.size() > poff + 25 + i ? c[25 + i] : 0;
    }
    ALOGE("bt_hci_translate: start qti_codec=%u rate=0x%04x acl=0x%04x l2c=0x%04x mtu=%u",
          codec_type_, sample_rate_, acl_hdl_, l2c_rcid_, mtu_);

    if (translation_multi()) {
      ALOGE("bt_hci_translate: multi forced, skipping single VSC");
      send_select_codec_locked();
      arm_watchdog(kAospOffloadStart);
      return;
    }

    // Single VSC: A2DP_OFFLOAD_START (0x0A) carries the transport selector.
    std::vector<uint8_t> params;
    params.push_back(kQtiOffloadStart);
    params.push_back(codec_type_);
    params.push_back(kTransportSlimbus);
    params.push_back(codec_type_);
    params.push_back(0x00);
    params.push_back(0x00);
    params.push_back(0x00);
    params.push_back(cp_active_);
    params.push_back(cp_flag_);
    params.push_back(static_cast<uint8_t>(sample_rate_ & 0xFF));
    params.push_back(static_cast<uint8_t>((sample_rate_ >> 8) & 0xFF));
    params.push_back(static_cast<uint8_t>(acl_hdl_ & 0xFF));
    params.push_back(static_cast<uint8_t>((acl_hdl_ >> 8) & 0xFF));
    params.push_back(static_cast<uint8_t>(l2c_rcid_ & 0xFF));
    params.push_back(static_cast<uint8_t>((l2c_rcid_ >> 8) & 0xFF));
    params.push_back(static_cast<uint8_t>(mtu_ & 0xFF));
    params.push_back(static_cast<uint8_t>((mtu_ >> 8) & 0xFF));
    params.push_back(0x01);  // stream_start
    params.push_back(0x00);  // split_acl
    params.push_back(0x00);  // ch_mode
    params.push_back(0x00);  // ttp
    params.push_back(0x00);
    for (size_t i = 0; i < sizeof(codec_info_); ++i) {
      params.push_back(codec_info_[i]);
    }
    send_qti_locked(params);
    step_ = Step::AwaitSingleStart;
    arm_watchdog(kAospOffloadStart);
  }

  void send_select_codec_locked() {
    send_qti_locked({kQtiSelectedCodec, codec_type_, 0x00, 0x00, cp_active_, cp_flag_,
                     static_cast<uint8_t>(sample_rate_ & 0xFF),
                     static_cast<uint8_t>((sample_rate_ >> 8) & 0xFF)});
    step_ = Step::AwaitSelectCodec;
  }

  void on_qti_cc_locked(uint8_t status, uint8_t subop, uint8_t evt_type) {
    ALOGE("bt_hci_translate: QTI event type=0x%02x subop=0x%02x status=0x%02x step=%d", evt_type,
          subop, status, static_cast<int>(step_));

    switch (step_) {
      case Step::AwaitSingleStart:
        if (status == 0) {
          gen_++;
          step_ = Step::Idle;
          synth_aosp_cc_locked(0, kAospOffloadStart);
        } else {
          // The single-VSC A2DP_OFFLOAD_START is not accepted by every firmware
          // (0x01 = invalid VSC, 0x12 = invalid params, ...); the QTI stack then
          // configures the transport with the multi-VSC chain instead.
          ALOGE("bt_hci_translate: single VSC rejected (0x%02x), trying multi-VSC", status);
          send_select_codec_locked();
        }
        break;
      case Step::AwaitSelectCodec:
        if (status != 0) {
          gen_++;
          step_ = Step::Idle;
          synth_aosp_cc_locked(status, kAospOffloadStart);
          break;
        }
        // QTI source, byte-for-byte: param[0]=0x07, param[1]=0 (slimbus),
        // param[2]=codec_type. Nothing else. Length 3.
        ALOGE("bt_hci_translate: sending 0x07 transport=slimbus codec=%u (len 3)", codec_type_);
        send_qti_locked({kQtiTransportConfig, kTransportSlimbus, codec_type_});
        step_ = Step::AwaitTransportConfig;
        break;
      case Step::AwaitTransportConfig:
        if (status != 0) {
          gen_++;
          step_ = Step::Idle;
          synth_aosp_cc_locked(status, kAospOffloadStart);
          break;
        }
        send_qti_locked({kQtiWriteMediaChannelCfg, 0x00,
                         static_cast<uint8_t>(acl_hdl_ & 0xFF),
                         static_cast<uint8_t>((acl_hdl_ >> 8) & 0xFF),
                         static_cast<uint8_t>(l2c_rcid_ & 0xFF),
                         static_cast<uint8_t>((l2c_rcid_ >> 8) & 0xFF),
                         static_cast<uint8_t>(mtu_ & 0xFF),
                         static_cast<uint8_t>((mtu_ >> 8) & 0xFF)});
        step_ = Step::AwaitMediaChannel;
        break;
      case Step::AwaitMediaChannel:
        if (status != 0) {
          gen_++;
          step_ = Step::Idle;
          synth_aosp_cc_locked(status, kAospOffloadStart);
          break;
        }
        if (cp_active_) {
          send_qti_locked({kQtiWriteScmsCp, cp_flag_});
        } else {
          send_qti_locked({kQtiStartA2dpMedia, 0x00});
        }
        step_ = Step::AwaitStartMedia;
        break;
      case Step::AwaitStartMedia:
        gen_++;
        step_ = Step::Idle;
        synth_aosp_cc_locked(status, kAospOffloadStart);
        if (status == 0 && read_cfg_enabled()) {
          ALOGE("bt_hci_translate: diagnostic READ_A2DP_CFG (0x01)");
          send_qti_locked({0x01});
        }
        break;
      case Step::Idle:
      default:
        break;
    }
  }

  sp<IBluetoothHci_1_0> real_;
  sp<IBluetoothHciCallbacks_1_0> stack_cb_;
  sp<ProxyCallbacks> proxy_cb_;
  std::recursive_mutex mutex_;
  Step step_ = Step::Idle;
  std::atomic<uint64_t> gen_{0};

  uint8_t codec_type_ = 0;
  uint16_t sample_rate_ = 0;
  uint8_t cp_active_ = 0;
  uint8_t cp_flag_ = 0;
  uint16_t acl_hdl_ = 0;
  uint16_t l2c_rcid_ = 0;
  uint16_t mtu_ = 0;
  uint8_t codec_info_[20] = {};
};

Return<void> ProxyCallbacks::initializationComplete(HidlStatus status) {
  owner_->forward_initialization(status);
  return Void();
}

Return<void> ProxyCallbacks::hciEventReceived(const hidl_vec<uint8_t>& packet) {
  owner_->forward_event(packet);
  return Void();
}

Return<void> ProxyCallbacks::aclDataReceived(const hidl_vec<uint8_t>& packet) {
  owner_->forward_acl(packet);
  return Void();
}

Return<void> ProxyCallbacks::scoDataReceived(const hidl_vec<uint8_t>& packet) {
  owner_->forward_sco(packet);
  return Void();
}

}  // namespace

extern "C" IBluetoothHci_1_0* HIDL_FETCH_IBluetoothHci(const char* name) {
#if defined(__LP64__)
  const char* path = "/vendor/lib64/android.hardware.bluetooth@1.0-impl-qti.so";
#else
  const char* path = "/vendor/lib/android.hardware.bluetooth@1.0-impl-qti.so";
#endif
  ALOGE("bt_hci_translate: HIDL_FETCH_IBluetoothHci name=%s path=%s", name, path);
  void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    ALOGE("bt_hci_translate: failed to load original impl %s: %s", path, dlerror());
    return nullptr;
  }
  using FetchFn = IBluetoothHci_1_0* (*)(const char*);
  auto fetch = reinterpret_cast<FetchFn>(dlsym(handle, "HIDL_FETCH_IBluetoothHci"));
  if (fetch == nullptr) {
    ALOGE("bt_hci_translate: original impl has no HIDL_FETCH_IBluetoothHci: %s", dlerror());
    return nullptr;
  }
  sp<IBluetoothHci_1_0> real = fetch(name);
  if (real == nullptr) {
    ALOGE("bt_hci_translate: original impl returned null");
    return nullptr;
  }
  if (!translation_enabled()) {
    ALOGE("bt_hci_translate: disabled by property, using original impl");
    return real.get();
  }
  ALOGE("bt_hci_translate: QTI SLIMbus A2DP offload translation enabled");
  return new TranslateHci(real);
}
