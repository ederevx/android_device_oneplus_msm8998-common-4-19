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

#include <aidl/android/hardware/bluetooth/audio/AudioCapabilities.h>
#include <aidl/android/hardware/bluetooth/audio/AudioConfiguration.h>
#include <aidl/android/hardware/bluetooth/audio/BluetoothAudioStatus.h>
#include <aidl/android/hardware/bluetooth/audio/SessionType.h>
#include <vendor/qti/hardware/bluetooth_audio/2.0/types.h>

namespace device {
namespace oneplus {
namespace bluetooth_audio {

// Translates between the AIDL Bluetooth audio types the Bluetooth stack speaks
// and the QTI HIDL 2.0 types the vendor provider expects.
class QtiAudioConfigConverter {
  public:
    using AidlSessionType = ::aidl::android::hardware::bluetooth::audio::SessionType;
    using AidlStatus = ::aidl::android::hardware::bluetooth::audio::BluetoothAudioStatus;
    using AidlAudioConfig = ::aidl::android::hardware::bluetooth::audio::AudioConfiguration;
    using AidlAudioCapabilities =
            ::aidl::android::hardware::bluetooth::audio::AudioCapabilities;

    using HidlSessionType = ::vendor::qti::hardware::bluetooth_audio::V2_0::SessionType;
    using HidlStatus = ::vendor::qti::hardware::bluetooth_audio::V2_0::Status;
    using HidlAudioConfig = ::vendor::qti::hardware::bluetooth_audio::V2_0::AudioConfiguration;
    using HidlAudioCapabilities =
            ::vendor::qti::hardware::bluetooth_audio::V2_0::AudioCapabilities;

    // Session types served by the QTI provider; everything else stays with the
    // AOSP providers.
    static bool IsQtiSessionType(const AidlSessionType& session_type);

    // Session type conversion.
    static HidlSessionType ToHidlSessionType(const AidlSessionType& session_type);

    // Status conversion.
    static HidlStatus ToHidlStatus(const AidlStatus& status);

    // Audio configuration conversion; returns false when the configuration is
    // not representable in the QTI HIDL 2.0 types.
    static bool ToHidlAudioConfig(const AidlAudioConfig& audio_config,
                                  HidlAudioConfig* hidl_audio_config);

    // Provider capability conversion. The vendor union carries no discriminator,
    // so the session type selects which member is valid.
    static AidlAudioCapabilities ToAidlAudioCapabilities(const AidlSessionType& session_type,
                                                         const HidlAudioCapabilities& capabilities);
};

}  // namespace bluetooth_audio
}  // namespace oneplus
}  // namespace device
