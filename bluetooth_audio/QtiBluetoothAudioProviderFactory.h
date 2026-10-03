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
#pragma once

#include <BluetoothAudioProviderFactory.h>
#include <aidl/android/hardware/bluetooth/audio/BnBluetoothAudioProviderFactory.h>
#include <vendor/qti/hardware/bluetooth_audio/2.0/IBluetoothAudioProvidersFactory.h>

namespace device {
namespace oneplus {
namespace bluetooth_audio {

// Dispatches the session types implemented on this device by the QTI HIDL 2.0
// provider to it, and leaves every other session type with the AOSP providers.
class QtiBluetoothAudioProviderFactory
    : public ::aidl::android::hardware::bluetooth::audio::BnBluetoothAudioProviderFactory {
  public:
    QtiBluetoothAudioProviderFactory();

    ndk::ScopedAStatus openProvider(
            const ::aidl::android::hardware::bluetooth::audio::SessionType session_type,
            std::shared_ptr<::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider>*
                    _aidl_return) override;
    ndk::ScopedAStatus getProviderCapabilities(
            const ::aidl::android::hardware::bluetooth::audio::SessionType session_type,
            std::vector<::aidl::android::hardware::bluetooth::audio::AudioCapabilities>*
                    _aidl_return) override;
    ndk::ScopedAStatus getProviderInfo(
            ::aidl::android::hardware::bluetooth::audio::SessionType session_type,
            std::optional<::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProviderFactory::
                                  ProviderInfo>* _aidl_return) override;

  private:
    // Lazily resolves the vendor provider; the HIDL implementation is a
    // passthrough library, so this loads it into this process next to the
    // session store it feeds.
    bool LoadQtiFactory();

    const ::aidl::android::hardware::bluetooth::audio::BluetoothAudioProviderFactory aosp_factory_;
    ::android::sp<::vendor::qti::hardware::bluetooth_audio::V2_0::IBluetoothAudioProvidersFactory>
            qti_factory_;
};

}  // namespace bluetooth_audio
}  // namespace oneplus
}  // namespace device
