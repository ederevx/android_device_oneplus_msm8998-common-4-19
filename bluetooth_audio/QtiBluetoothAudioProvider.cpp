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
#define LOG_TAG "BtAudioQtiProvider"

#include "QtiBluetoothAudioProvider.h"

#include <fmq/ConvertMQDescriptors.h>
#include <android-base/logging.h>

#include <future>

#include "QtiAudioConfigConverter.h"
#include "QtiAudioPortForwarder.h"

namespace device {
namespace oneplus {
namespace bluetooth_audio {

using AidlAudioConfiguration =
        ::aidl::android::hardware::bluetooth::audio::AudioConfiguration;
using AidlBluetoothAudioStatus =
        ::aidl::android::hardware::bluetooth::audio::BluetoothAudioStatus;
using HidlDataMQDescriptor = ::android::hardware::MQDescriptorSync<unsigned char>;
using HidlStatus = ::vendor::qti::hardware::bluetooth_audio::V2_0::Status;

namespace {

struct DeathCookie {
    std::shared_ptr<QtiBluetoothAudioProvider> provider;
};

void OnBinderUnlinked(void* cookie) {
    delete static_cast<DeathCookie*>(cookie);
}

void OnBinderDied(void* cookie) {
    auto* death_cookie = static_cast<DeathCookie*>(cookie);
    CHECK(death_cookie != nullptr);
    death_cookie->provider->endSession();
}

ndk::ScopedAStatus ToAidlStatus(HidlStatus status) {
    switch (status) {
        case HidlStatus::SUCCESS:
            return ndk::ScopedAStatus::ok();
        case HidlStatus::UNSUPPORTED_CODEC_CONFIGURATION:
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        default:
            return ndk::ScopedAStatus::fromExceptionCode(EX_SERVICE_SPECIFIC);
    }
}

ndk::ScopedAStatus UnsupportedLeAudio() {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

}  // namespace

QtiBluetoothAudioProvider::QtiBluetoothAudioProvider(
        const ::android::sp<
                ::vendor::qti::hardware::bluetooth_audio::V2_0::IBluetoothAudioProvider>& provider)
    : provider_(provider) {
    death_recipient_ = ::ndk::ScopedAIBinder_DeathRecipient(
            AIBinder_DeathRecipient_new(OnBinderDied));
    AIBinder_DeathRecipient_setOnUnlinked(death_recipient_.get(), OnBinderUnlinked);
}

ndk::ScopedAStatus QtiBluetoothAudioProvider::startSession(
        const std::shared_ptr<
                ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioPort>& host_if,
        const AidlAudioConfiguration& audio_config,
        const std::vector<::aidl::android::hardware::bluetooth::audio::LatencyMode>& latency_modes,
        AidlDataMQDesc* _aidl_return) {
    (void)latency_modes;
    *_aidl_return = AidlDataMQDesc();
    if (host_if == nullptr) {
        LOG(ERROR) << __func__ << ": no Bluetooth audio port";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    QtiAudioConfigConverter::HidlAudioConfig hidl_audio_config;
    if (!QtiAudioConfigConverter::ToHidlAudioConfig(audio_config, &hidl_audio_config)) {
        LOG(ERROR) << __func__ << ": unsupported audio configuration";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    std::promise<void> hidl_call;
    auto hidl_call_future = hidl_call.get_future();
    HidlStatus session_status = HidlStatus::FAILURE;
    const auto hidl_cb = [&session_status, &hidl_call, &_aidl_return](
                                 HidlStatus status, const HidlDataMQDescriptor& data_mq) {
        session_status = status;
        if (status == HidlStatus::SUCCESS && data_mq.isHandleValid()) {
            ::android::unsafeHidlToAidlMQDescriptor<unsigned char, int8_t,
                                                    ::aidl::android::hardware::common::fmq::
                                                            SynchronizedReadWrite>(data_mq,
                                                                                   _aidl_return);
        }
        hidl_call.set_value();
    };

    const ::android::sp<QtiAudioPortForwarder> hidl_port =
            ::android::sp<QtiAudioPortForwarder>::make(host_if);
    provider_->startSession(hidl_port, hidl_audio_config, hidl_cb);
    hidl_call_future.get();

    if (session_status != HidlStatus::SUCCESS) {
        LOG(ERROR) << __func__ << ": vendor provider failed, status="
                   << static_cast<int32_t>(session_status);
        return ToAidlStatus(session_status);
    }
    stack_iface_ = host_if;
    AIBinder_linkToDeath(
            stack_iface_->asBinder().get(), death_recipient_.get(),
            new DeathCookie{ref<QtiBluetoothAudioProvider>()});
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus QtiBluetoothAudioProvider::endSession() {
    if (stack_iface_ != nullptr) {
        AIBinder_unlinkToDeath(stack_iface_->asBinder().get(), death_recipient_.get(), this);
        stack_iface_ = nullptr;
    }
    provider_->endSession();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus QtiBluetoothAudioProvider::streamStarted(AidlBluetoothAudioStatus status) {
    provider_->streamStarted(QtiAudioConfigConverter::ToHidlStatus(status));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus QtiBluetoothAudioProvider::streamSuspended(AidlBluetoothAudioStatus status) {
    provider_->streamSuspended(QtiAudioConfigConverter::ToHidlStatus(status));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus QtiBluetoothAudioProvider::updateAudioConfiguration(
        const AidlAudioConfiguration& audio_config) {
    // The vendor provider learns the configuration when the session starts.
    (void)audio_config;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus QtiBluetoothAudioProvider::setLowLatencyModeAllowed(bool allowed) {
    // The vendor provider has no low latency mode control.
    (void)allowed;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus QtiBluetoothAudioProvider::parseA2dpConfiguration(
        const ::aidl::android::hardware::bluetooth::audio::CodecId& codec_id,
        const std::vector<uint8_t>& configuration,
        ::aidl::android::hardware::bluetooth::audio::CodecParameters* codec_parameters,
        ::aidl::android::hardware::bluetooth::audio::A2dpStatus* _aidl_return) {
    (void)codec_id;
    (void)configuration;
    (void)codec_parameters;
    (void)_aidl_return;
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
}

ndk::ScopedAStatus QtiBluetoothAudioProvider::getA2dpConfiguration(
        const std::vector<
                ::aidl::android::hardware::bluetooth::audio::A2dpRemoteCapabilities>&
                remote_a2dp_capabilities,
        const ::aidl::android::hardware::bluetooth::audio::A2dpConfigurationHint& hint,
        std::optional<::aidl::android::hardware::bluetooth::audio::A2dpConfiguration>*
                _aidl_return) {
    (void)remote_a2dp_capabilities;
    (void)hint;
    (void)_aidl_return;
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
}

ndk::ScopedAStatus QtiBluetoothAudioProvider::setCodecPriority(
        const ::aidl::android::hardware::bluetooth::audio::CodecId& codec_id, int32_t priority) {
    (void)codec_id;
    (void)priority;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus QtiBluetoothAudioProvider::getLeAudioAseConfiguration(
        const std::optional<std::vector<std::optional<
                ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                        LeAudioDeviceCapabilities>>>& remote_sink_audio_capabilities,
        const std::optional<std::vector<std::optional<
                ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                        LeAudioDeviceCapabilities>>>& remote_source_audio_capabilities,
        const std::vector<::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                                  LeAudioConfigurationRequirement>& requirements,
        std::vector<::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                            LeAudioAseConfigurationSetting>* _aidl_return) {
    (void)remote_sink_audio_capabilities;
    (void)remote_source_audio_capabilities;
    (void)requirements;
    (void)_aidl_return;
    return UnsupportedLeAudio();
}

ndk::ScopedAStatus QtiBluetoothAudioProvider::getLeAudioAseQosConfiguration(
        const ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                LeAudioAseQosConfigurationRequirement& qos_requirement,
        ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                LeAudioAseQosConfigurationPair* _aidl_return) {
    (void)qos_requirement;
    (void)_aidl_return;
    return UnsupportedLeAudio();
}

ndk::ScopedAStatus QtiBluetoothAudioProvider::getLeAudioAseDatapathConfiguration(
        const std::optional<
                ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::StreamConfig>&
                sink_config,
        const std::optional<
                ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::StreamConfig>&
                source_config,
        ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                LeAudioDataPathConfigurationPair* _aidl_return) {
    (void)sink_config;
    (void)source_config;
    (void)_aidl_return;
    return UnsupportedLeAudio();
}

ndk::ScopedAStatus QtiBluetoothAudioProvider::onSinkAseMetadataChanged(
        ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::AseState state,
        int32_t cig_id, int32_t cis_id,
        const std::optional<std::vector<std::optional<
                ::aidl::android::hardware::bluetooth::audio::MetadataLtv>>>& metadata) {
    (void)state;
    (void)cig_id;
    (void)cis_id;
    (void)metadata;
    return UnsupportedLeAudio();
}

ndk::ScopedAStatus QtiBluetoothAudioProvider::onSourceAseMetadataChanged(
        ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::AseState state,
        int32_t cig_id, int32_t cis_id,
        const std::optional<std::vector<std::optional<
                ::aidl::android::hardware::bluetooth::audio::MetadataLtv>>>& metadata) {
    (void)state;
    (void)cig_id;
    (void)cis_id;
    (void)metadata;
    return UnsupportedLeAudio();
}

ndk::ScopedAStatus QtiBluetoothAudioProvider::getLeAudioBroadcastConfiguration(
        const std::optional<std::vector<std::optional<
                ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                        LeAudioDeviceCapabilities>>>& remote_sink_audio_capabilities,
        const ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                LeAudioBroadcastConfigurationRequirement& requirement,
        ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                LeAudioBroadcastConfigurationSetting* _aidl_return) {
    (void)remote_sink_audio_capabilities;
    (void)requirement;
    (void)_aidl_return;
    return UnsupportedLeAudio();
}

ndk::ScopedAStatus QtiBluetoothAudioProvider::getLeAudioBroadcastDatapathConfiguration(
        const ::aidl::android::hardware::bluetooth::audio::AudioContext& context,
        const std::vector<::aidl::android::hardware::bluetooth::audio::
                                  LeAudioBroadcastConfiguration::BroadcastStreamMap>& stream_map,
        ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                LeAudioDataPathConfiguration* _aidl_return) {
    (void)context;
    (void)stream_map;
    (void)_aidl_return;
    return UnsupportedLeAudio();
}

}  // namespace bluetooth_audio
}  // namespace oneplus
}  // namespace device
