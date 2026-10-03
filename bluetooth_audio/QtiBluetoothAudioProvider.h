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

#include <aidl/android/hardware/bluetooth/audio/BnBluetoothAudioProvider.h>
#include <aidl/android/hardware/common/fmq/MQDescriptor.h>
#include <aidl/android/hardware/common/fmq/SynchronizedReadWrite.h>
#include <vendor/qti/hardware/bluetooth_audio/2.0/IBluetoothAudioProvider.h>

namespace device {
namespace oneplus {
namespace bluetooth_audio {

using AidlDataMQDesc =
        ::aidl::android::hardware::common::fmq::MQDescriptor<int8_t,
                                                             ::aidl::android::hardware::common::
                                                                     fmq::SynchronizedReadWrite>;

// Presents a QTI HIDL 2.0 audio provider to the AIDL Bluetooth stack, so that
// the session is registered in the vendor session store the
// audio.bluetooth_qti HAL module and the primary HAL read.
class QtiBluetoothAudioProvider
    : public ::aidl::android::hardware::bluetooth::audio::BnBluetoothAudioProvider {
  public:
    explicit QtiBluetoothAudioProvider(
            const ::android::sp<::vendor::qti::hardware::bluetooth_audio::V2_0::
                                        IBluetoothAudioProvider>& provider);

    ndk::ScopedAStatus startSession(
            const std::shared_ptr<
                    ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioPort>& host_if,
            const ::aidl::android::hardware::bluetooth::audio::AudioConfiguration& audio_config,
            const std::vector<::aidl::android::hardware::bluetooth::audio::LatencyMode>&
                    latency_modes,
            AidlDataMQDesc* _aidl_return) override;
    ndk::ScopedAStatus endSession() override;
    ndk::ScopedAStatus streamStarted(
            ::aidl::android::hardware::bluetooth::audio::BluetoothAudioStatus status) override;
    ndk::ScopedAStatus streamSuspended(
            ::aidl::android::hardware::bluetooth::audio::BluetoothAudioStatus status) override;
    ndk::ScopedAStatus updateAudioConfiguration(
            const ::aidl::android::hardware::bluetooth::audio::AudioConfiguration& audio_config)
            override;
    ndk::ScopedAStatus setLowLatencyModeAllowed(bool allowed) override;
    ndk::ScopedAStatus parseA2dpConfiguration(
            const ::aidl::android::hardware::bluetooth::audio::CodecId& codec_id,
            const std::vector<uint8_t>& configuration,
            ::aidl::android::hardware::bluetooth::audio::CodecParameters* codec_parameters,
            ::aidl::android::hardware::bluetooth::audio::A2dpStatus* _aidl_return) override;
    ndk::ScopedAStatus getA2dpConfiguration(
            const std::vector<
                    ::aidl::android::hardware::bluetooth::audio::A2dpRemoteCapabilities>&
                    remote_a2dp_capabilities,
            const ::aidl::android::hardware::bluetooth::audio::A2dpConfigurationHint& hint,
            std::optional<::aidl::android::hardware::bluetooth::audio::A2dpConfiguration>*
                    _aidl_return) override;
    ndk::ScopedAStatus setCodecPriority(
            const ::aidl::android::hardware::bluetooth::audio::CodecId& codec_id,
            int32_t priority) override;
    ndk::ScopedAStatus getLeAudioAseConfiguration(
            const std::optional<std::vector<std::optional<
                    ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                            LeAudioDeviceCapabilities>>>& remote_sink_audio_capabilities,
            const std::optional<std::vector<std::optional<
                    ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                            LeAudioDeviceCapabilities>>>& remote_source_audio_capabilities,
            const std::vector<::aidl::android::hardware::bluetooth::audio::
                                      IBluetoothAudioProvider::LeAudioConfigurationRequirement>&
                    requirements,
            std::vector<::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                                LeAudioAseConfigurationSetting>* _aidl_return) override;
    ndk::ScopedAStatus getLeAudioAseQosConfiguration(
            const ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                    LeAudioAseQosConfigurationRequirement& qos_requirement,
            ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                    LeAudioAseQosConfigurationPair* _aidl_return) override;
    ndk::ScopedAStatus getLeAudioAseDatapathConfiguration(
            const std::optional<::aidl::android::hardware::bluetooth::audio::
                                        IBluetoothAudioProvider::StreamConfig>& sink_config,
            const std::optional<::aidl::android::hardware::bluetooth::audio::
                                        IBluetoothAudioProvider::StreamConfig>& source_config,
            ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                    LeAudioDataPathConfigurationPair* _aidl_return) override;
    ndk::ScopedAStatus onSinkAseMetadataChanged(
            ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::AseState state,
            int32_t cig_id, int32_t cis_id,
            const std::optional<std::vector<std::optional<
                    ::aidl::android::hardware::bluetooth::audio::MetadataLtv>>>& metadata)
            override;
    ndk::ScopedAStatus onSourceAseMetadataChanged(
            ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::AseState state,
            int32_t cig_id, int32_t cis_id,
            const std::optional<std::vector<std::optional<
                    ::aidl::android::hardware::bluetooth::audio::MetadataLtv>>>& metadata)
            override;
    ndk::ScopedAStatus getLeAudioBroadcastConfiguration(
            const std::optional<std::vector<std::optional<
                    ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                            LeAudioDeviceCapabilities>>>& remote_sink_audio_capabilities,
            const ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                    LeAudioBroadcastConfigurationRequirement& requirement,
            ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                    LeAudioBroadcastConfigurationSetting* _aidl_return) override;
    ndk::ScopedAStatus getLeAudioBroadcastDatapathConfiguration(
            const ::aidl::android::hardware::bluetooth::audio::AudioContext& context,
            const std::vector<::aidl::android::hardware::bluetooth::audio::
                                      LeAudioBroadcastConfiguration::BroadcastStreamMap>& stream_map,
            ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider::
                    LeAudioDataPathConfiguration* _aidl_return) override;

  private:
    const ::android::sp<
            ::vendor::qti::hardware::bluetooth_audio::V2_0::IBluetoothAudioProvider>
            provider_;
    std::shared_ptr<::aidl::android::hardware::bluetooth::audio::IBluetoothAudioPort> stack_iface_;
    ::ndk::ScopedAIBinder_DeathRecipient death_recipient_;
};

}  // namespace bluetooth_audio
}  // namespace oneplus
}  // namespace device
