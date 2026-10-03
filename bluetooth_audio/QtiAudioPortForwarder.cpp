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
#define LOG_TAG "BtAudioQtiPort"

#include "QtiAudioPortForwarder.h"

#include <android-base/logging.h>

namespace device {
namespace oneplus {
namespace bluetooth_audio {

using AidlPresentationPosition =
        ::aidl::android::hardware::bluetooth::audio::PresentationPosition;
using HidlStatus = ::vendor::qti::hardware::bluetooth_audio::V2_0::Status;
using HidlTimeSpec = ::vendor::qti::hardware::bluetooth_audio::V2_0::TimeSpec;

QtiAudioPortForwarder::QtiAudioPortForwarder(
        const std::shared_ptr<
                ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioPort>& port)
    : port_(port) {}

::android::hardware::Return<void> QtiAudioPortForwarder::startStream() {
    port_->startStream(false);
    return ::android::hardware::Void();
}

::android::hardware::Return<void> QtiAudioPortForwarder::stopStream() {
    port_->stopStream();
    return ::android::hardware::Void();
}

::android::hardware::Return<void> QtiAudioPortForwarder::suspendStream() {
    port_->suspendStream();
    return ::android::hardware::Void();
}

::android::hardware::Return<void> QtiAudioPortForwarder::getPresentationPosition(
        getPresentationPosition_cb _hidl_cb) {
    AidlPresentationPosition position;
    const auto status = port_->getPresentationPosition(&position);
    if (!status.isOk()) {
        _hidl_cb(HidlStatus::FAILURE, 0, 0, HidlTimeSpec{.tvSec = 0, .tvNSec = 0});
        return ::android::hardware::Void();
    }
    _hidl_cb(HidlStatus::SUCCESS, position.remoteDeviceAudioDelayNanos,
             position.transmittedOctets,
             HidlTimeSpec{
                     .tvSec = static_cast<uint64_t>(position.transmittedOctetsTimestamp.tvSec),
                     .tvNSec = static_cast<uint64_t>(position.transmittedOctetsTimestamp.tvNSec)});
    return ::android::hardware::Void();
}

::android::hardware::Return<void> QtiAudioPortForwarder::updateAptxMode(uint16_t aptxMode) {
    // The AIDL Bluetooth audio interface has no equivalent notification.
    LOG(INFO) << __func__ << ": ignoring aptX mode " << aptxMode;
    return ::android::hardware::Void();
}

}  // namespace bluetooth_audio
}  // namespace oneplus
}  // namespace device
