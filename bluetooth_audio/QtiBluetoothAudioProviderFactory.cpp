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
#define LOG_TAG "BtAudioQtiFactory"

#include "QtiBluetoothAudioProviderFactory.h"

#include <android-base/logging.h>

#include <future>

#include "QtiAudioConfigConverter.h"
#include "QtiBluetoothAudioProvider.h"

namespace device {
namespace oneplus {
namespace bluetooth_audio {

using AidlAudioCapabilities =
        ::aidl::android::hardware::bluetooth::audio::AudioCapabilities;
using AidlIBluetoothAudioProvider =
        ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider;
using AidlSessionType = ::aidl::android::hardware::bluetooth::audio::SessionType;
using HidlAudioCapabilities =
        ::vendor::qti::hardware::bluetooth_audio::V2_0::AudioCapabilities;
using HidlIBluetoothAudioProvider =
        ::vendor::qti::hardware::bluetooth_audio::V2_0::IBluetoothAudioProvider;
using HidlStatus = ::vendor::qti::hardware::bluetooth_audio::V2_0::Status;

QtiBluetoothAudioProviderFactory::QtiBluetoothAudioProviderFactory() {}

bool QtiBluetoothAudioProviderFactory::LoadQtiFactory() {
    if (qti_factory_ != nullptr) {
        return true;
    }
    // The vendor implementation is a passthrough HIDL library; loading it here
    // puts the session store it writes in the process that serves both the
    // audio.bluetooth_qti HAL module and the primary HAL.
    qti_factory_ = ::vendor::qti::hardware::bluetooth_audio::V2_0::
            IBluetoothAudioProvidersFactory::getService("default");
    if (qti_factory_ == nullptr) {
        qti_factory_ = ::vendor::qti::hardware::bluetooth_audio::V2_0::
                IBluetoothAudioProvidersFactory::getService("default", true /* getStub */);
    }
    if (qti_factory_ == nullptr) {
        LOG(ERROR) << __func__ << ": vendor bluetooth audio provider unavailable";
        return false;
    }
    return true;
}

ndk::ScopedAStatus QtiBluetoothAudioProviderFactory::openProvider(
        const AidlSessionType session_type,
        std::shared_ptr<AidlIBluetoothAudioProvider>* _aidl_return) {
    if (!QtiAudioConfigConverter::IsQtiSessionType(session_type)) {
        return aosp_factory_.openProvider(session_type, _aidl_return);
    }
    if (!LoadQtiFactory()) {
        return aosp_factory_.openProvider(session_type, _aidl_return);
    }

    const auto hidl_session_type = QtiAudioConfigConverter::ToHidlSessionType(session_type);
    std::promise<void> hidl_call;
    auto hidl_call_future = hidl_call.get_future();
    HidlStatus provider_status = HidlStatus::FAILURE;
    ::android::sp<HidlIBluetoothAudioProvider> hidl_provider;
    qti_factory_->openProvider(
            hidl_session_type,
            [&provider_status, &hidl_provider, &hidl_call](
                    HidlStatus status, const ::android::sp<HidlIBluetoothAudioProvider>& provider) {
                provider_status = status;
                hidl_provider = provider;
                hidl_call.set_value();
            });
    hidl_call_future.get();

    if (provider_status != HidlStatus::SUCCESS || hidl_provider == nullptr) {
        LOG(ERROR) << __func__ << ": vendor provider refused session type "
                   << static_cast<int32_t>(session_type)
                   << ", status=" << static_cast<int32_t>(provider_status);
        return ndk::ScopedAStatus::fromExceptionCode(EX_SERVICE_SPECIFIC);
    }
    *_aidl_return = ::ndk::SharedRefBase::make<QtiBluetoothAudioProvider>(hidl_provider);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus QtiBluetoothAudioProviderFactory::getProviderCapabilities(
        const AidlSessionType session_type, std::vector<AidlAudioCapabilities>* _aidl_return) {
    if (!QtiAudioConfigConverter::IsQtiSessionType(session_type)) {
        return aosp_factory_.getProviderCapabilities(session_type, _aidl_return);
    }
    if (!LoadQtiFactory()) {
        return aosp_factory_.getProviderCapabilities(session_type, _aidl_return);
    }

    const auto hidl_session_type = QtiAudioConfigConverter::ToHidlSessionType(session_type);
    std::promise<void> hidl_call;
    auto hidl_call_future = hidl_call.get_future();
    qti_factory_->getProviderCapabilities(
            hidl_session_type,
            [&hidl_call, &_aidl_return](const ::android::hardware::hidl_vec<
                                                HidlAudioCapabilities>& capabilities) {
                _aidl_return->clear();
                _aidl_return->reserve(capabilities.size());
                for (const auto& capability : capabilities) {
                    _aidl_return->push_back(
                            QtiAudioConfigConverter::ToAidlAudioCapabilities(capability));
                }
                hidl_call.set_value();
            });
    hidl_call_future.get();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus QtiBluetoothAudioProviderFactory::getProviderInfo(
        AidlSessionType session_type,
        std::optional<::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProviderFactory::
                              ProviderInfo>* _aidl_return) {
    if (session_type == AidlSessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH ||
        session_type == AidlSessionType::A2DP_HARDWARE_OFFLOAD_DECODING_DATAPATH) {
        // Answering this call would switch the stack to A2DP codec
        // extensibility, which needs the provider to negotiate the codec
        // itself; the vendor provider takes the negotiated configuration
        // instead. Keep the stack on the legacy codec configuration path.
        *_aidl_return = std::nullopt;
        return ndk::ScopedAStatus::fromStatus(STATUS_UNKNOWN_TRANSACTION);
    }
    return aosp_factory_.getProviderInfo(session_type, _aidl_return);
}

}  // namespace bluetooth_audio
}  // namespace oneplus
}  // namespace device
