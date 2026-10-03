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
#define LOG_TAG "BtAudioQtiConverter"

#include "QtiAudioConfigConverter.h"

#include <aidl/android/hardware/bluetooth/audio/AacCapabilities.h>
#include <aidl/android/hardware/bluetooth/audio/AacConfiguration.h>
#include <aidl/android/hardware/bluetooth/audio/AptxCapabilities.h>
#include <aidl/android/hardware/bluetooth/audio/AptxConfiguration.h>
#include <aidl/android/hardware/bluetooth/audio/CodecCapabilities.h>
#include <aidl/android/hardware/bluetooth/audio/CodecConfiguration.h>
#include <aidl/android/hardware/bluetooth/audio/LdacCapabilities.h>
#include <aidl/android/hardware/bluetooth/audio/LdacConfiguration.h>
#include <aidl/android/hardware/bluetooth/audio/PcmCapabilities.h>
#include <aidl/android/hardware/bluetooth/audio/PcmConfiguration.h>
#include <aidl/android/hardware/bluetooth/audio/SbcCapabilities.h>
#include <aidl/android/hardware/bluetooth/audio/SbcConfiguration.h>
#include <android-base/logging.h>

namespace device {
namespace oneplus {
namespace bluetooth_audio {

using AidlAacConfiguration = ::aidl::android::hardware::bluetooth::audio::AacConfiguration;
using AidlAptxConfiguration = ::aidl::android::hardware::bluetooth::audio::AptxConfiguration;
using AidlCodecConfiguration = ::aidl::android::hardware::bluetooth::audio::CodecConfiguration;
using AidlLdacConfiguration = ::aidl::android::hardware::bluetooth::audio::LdacConfiguration;
using AidlPcmConfiguration = ::aidl::android::hardware::bluetooth::audio::PcmConfiguration;
using AidlSbcConfiguration = ::aidl::android::hardware::bluetooth::audio::SbcConfiguration;

using HidlAacParameters = ::vendor::qti::hardware::bluetooth_audio::V2_0::AacParameters;
using HidlAptxParameters = ::vendor::qti::hardware::bluetooth_audio::V2_0::AptxParameters;
using HidlAudioCapabilities = ::vendor::qti::hardware::bluetooth_audio::V2_0::AudioCapabilities;
using HidlBitsPerSample = ::vendor::qti::hardware::bluetooth_audio::V2_0::BitsPerSample;
using HidlChannelMode = ::vendor::qti::hardware::bluetooth_audio::V2_0::ChannelMode;
using HidlCodecCapabilities = ::vendor::qti::hardware::bluetooth_audio::V2_0::CodecCapabilities;
using HidlCodecConfiguration = ::vendor::qti::hardware::bluetooth_audio::V2_0::CodecConfiguration;
using HidlCodecType = ::vendor::qti::hardware::bluetooth_audio::V2_0::CodecType;
using HidlLdacParameters = ::vendor::qti::hardware::bluetooth_audio::V2_0::LdacParameters;
using HidlPcmParameters = ::vendor::qti::hardware::bluetooth_audio::V2_0::PcmParameters;
using HidlSbcParameters = ::vendor::qti::hardware::bluetooth_audio::V2_0::SbcParameters;
using HidlSampleRate = ::vendor::qti::hardware::bluetooth_audio::V2_0::SampleRate;

namespace {

HidlSampleRate ToHidlSampleRate(int32_t sample_rate_hz) {
    switch (sample_rate_hz) {
        case 44100:
            return HidlSampleRate::RATE_44100;
        case 48000:
            return HidlSampleRate::RATE_48000;
        case 88200:
            return HidlSampleRate::RATE_88200;
        case 96000:
            return HidlSampleRate::RATE_96000;
        case 176400:
            return HidlSampleRate::RATE_176400;
        case 192000:
            return HidlSampleRate::RATE_192000;
        case 16000:
            return HidlSampleRate::RATE_16000;
        case 24000:
            return HidlSampleRate::RATE_24000;
        case 32000:
            return HidlSampleRate::RATE_32000;
        default:
            return HidlSampleRate::RATE_UNKNOWN;
    }
}

std::vector<int32_t> ToAidlSampleRates(HidlSampleRate sample_rate) {
    // The vendor bitfield packs 24000 and 32000 into the high bits.
    const std::pair<HidlSampleRate, int32_t> kRates[] = {
            {HidlSampleRate::RATE_44100, 44100},   {HidlSampleRate::RATE_48000, 48000},
            {HidlSampleRate::RATE_88200, 88200},   {HidlSampleRate::RATE_96000, 96000},
            {HidlSampleRate::RATE_176400, 176400}, {HidlSampleRate::RATE_192000, 192000},
            {HidlSampleRate::RATE_16000, 16000},   {HidlSampleRate::RATE_24000, 24000},
            {HidlSampleRate::RATE_32000, 32000},
    };
    std::vector<int32_t> rates;
    for (const auto& [bit, value] : kRates) {
        if (static_cast<uint32_t>(sample_rate) & static_cast<uint32_t>(bit)) {
            rates.push_back(value);
        }
    }
    return rates;
}

HidlBitsPerSample ToHidlBitsPerSample(int8_t bits_per_sample) {
    switch (bits_per_sample) {
        case 16:
            return HidlBitsPerSample::BITS_16;
        case 24:
            return HidlBitsPerSample::BITS_24;
        case 32:
            return HidlBitsPerSample::BITS_32;
        default:
            return HidlBitsPerSample::BITS_UNKNOWN;
    }
}

std::vector<uint8_t> ToAidlBitsPerSample(HidlBitsPerSample bits_per_sample) {
    std::vector<uint8_t> bits;
    if (static_cast<uint32_t>(bits_per_sample) &
        static_cast<uint32_t>(HidlBitsPerSample::BITS_16)) {
        bits.push_back(16);
    }
    if (static_cast<uint32_t>(bits_per_sample) &
        static_cast<uint32_t>(HidlBitsPerSample::BITS_24)) {
        bits.push_back(24);
    }
    if (static_cast<uint32_t>(bits_per_sample) &
        static_cast<uint32_t>(HidlBitsPerSample::BITS_32)) {
        bits.push_back(32);
    }
    return bits;
}

HidlChannelMode ToHidlChannelMode(
        ::aidl::android::hardware::bluetooth::audio::ChannelMode channel_mode) {
    using AidlChannelMode = ::aidl::android::hardware::bluetooth::audio::ChannelMode;
    switch (channel_mode) {
        case AidlChannelMode::MONO:
            return HidlChannelMode::MONO;
        case AidlChannelMode::STEREO:
            return HidlChannelMode::STEREO;
        default:
            return HidlChannelMode::UNKNOWN;
    }
}

std::vector<::aidl::android::hardware::bluetooth::audio::ChannelMode> ToAidlChannelModes(
        HidlChannelMode channel_mode) {
    using AidlChannelMode = ::aidl::android::hardware::bluetooth::audio::ChannelMode;
    std::vector<AidlChannelMode> modes;
    if (static_cast<uint32_t>(channel_mode) & static_cast<uint32_t>(HidlChannelMode::MONO)) {
        modes.push_back(AidlChannelMode::MONO);
    }
    if (static_cast<uint32_t>(channel_mode) & static_cast<uint32_t>(HidlChannelMode::STEREO)) {
        modes.push_back(AidlChannelMode::STEREO);
    }
    return modes;
}

HidlCodecType ToHidlCodecType(::aidl::android::hardware::bluetooth::audio::CodecType codec_type) {
    using AidlCodecType = ::aidl::android::hardware::bluetooth::audio::CodecType;
    switch (codec_type) {
        case AidlCodecType::SBC:
            return HidlCodecType::SBC;
        case AidlCodecType::AAC:
            return HidlCodecType::AAC;
        case AidlCodecType::APTX:
            return HidlCodecType::APTX;
        case AidlCodecType::APTX_HD:
            return HidlCodecType::APTX_HD;
        case AidlCodecType::LDAC:
            return HidlCodecType::LDAC;
        default:
            return HidlCodecType::UNKNOWN;
    }
}

::aidl::android::hardware::bluetooth::audio::CodecType ToAidlCodecType(HidlCodecType codec_type) {
    using AidlCodecType = ::aidl::android::hardware::bluetooth::audio::CodecType;
    switch (codec_type) {
        case HidlCodecType::SBC:
            return AidlCodecType::SBC;
        case HidlCodecType::AAC:
            return AidlCodecType::AAC;
        case HidlCodecType::APTX:
            return AidlCodecType::APTX;
        case HidlCodecType::APTX_HD:
            return AidlCodecType::APTX_HD;
        case HidlCodecType::LDAC:
            return AidlCodecType::LDAC;
        default:
            return AidlCodecType::UNKNOWN;
    }
}

HidlPcmParameters ToHidlPcmParameters(const AidlPcmConfiguration& pcm_config) {
    HidlPcmParameters parameters;
    parameters.sampleRate = ToHidlSampleRate(pcm_config.sampleRateHz);
    parameters.channelMode = ToHidlChannelMode(pcm_config.channelMode);
    parameters.bitsPerSample = ToHidlBitsPerSample(pcm_config.bitsPerSample);
    return parameters;
}

HidlSbcParameters ToHidlSbcParameters(const AidlSbcConfiguration& sbc_config) {
    using AidlSbcAllocMethod = ::aidl::android::hardware::bluetooth::audio::SbcAllocMethod;
    using AidlSbcChannelMode = ::aidl::android::hardware::bluetooth::audio::SbcChannelMode;
    using HidlSbcAllocMethod = ::vendor::qti::hardware::bluetooth_audio::V2_0::SbcAllocMethod;
    using HidlSbcBlockLength = ::vendor::qti::hardware::bluetooth_audio::V2_0::SbcBlockLength;
    using HidlSbcChannelMode = ::vendor::qti::hardware::bluetooth_audio::V2_0::SbcChannelMode;
    using HidlSbcNumSubbands = ::vendor::qti::hardware::bluetooth_audio::V2_0::SbcNumSubbands;

    HidlSbcParameters parameters;
    parameters.sampleRate = ToHidlSampleRate(sbc_config.sampleRateHz);
    parameters.bitsPerSample = ToHidlBitsPerSample(sbc_config.bitsPerSample);
    parameters.minBitpool = sbc_config.minBitpool;
    parameters.maxBitpool = sbc_config.maxBitpool;
    switch (sbc_config.channelMode) {
        case AidlSbcChannelMode::JOINT_STEREO:
            parameters.channelMode = HidlSbcChannelMode::JOINT_STEREO;
            break;
        case AidlSbcChannelMode::STEREO:
            parameters.channelMode = HidlSbcChannelMode::STEREO;
            break;
        case AidlSbcChannelMode::DUAL:
            parameters.channelMode = HidlSbcChannelMode::DUAL;
            break;
        case AidlSbcChannelMode::MONO:
            parameters.channelMode = HidlSbcChannelMode::MONO;
            break;
        default:
            parameters.channelMode = HidlSbcChannelMode::UNKNOWN;
            break;
    }
    switch (sbc_config.blockLength) {
        case 4:
            parameters.blockLength = HidlSbcBlockLength::BLOCKS_4;
            break;
        case 8:
            parameters.blockLength = HidlSbcBlockLength::BLOCKS_8;
            break;
        case 12:
            parameters.blockLength = HidlSbcBlockLength::BLOCKS_12;
            break;
        default:
            parameters.blockLength = HidlSbcBlockLength::BLOCKS_16;
            break;
    }
    switch (sbc_config.numSubbands) {
        case 4:
            parameters.numSubbands = HidlSbcNumSubbands::SUBBAND_4;
            break;
        default:
            parameters.numSubbands = HidlSbcNumSubbands::SUBBAND_8;
            break;
    }
    parameters.allocMethod = sbc_config.allocMethod == AidlSbcAllocMethod::ALLOC_MD_S
                                     ? HidlSbcAllocMethod::ALLOC_MD_S
                                     : HidlSbcAllocMethod::ALLOC_MD_L;
    return parameters;
}

HidlAacParameters ToHidlAacParameters(const AidlAacConfiguration& aac_config) {
    using AidlAacObjectType = ::aidl::android::hardware::bluetooth::audio::AacObjectType;
    using HidlAacObjectType = ::vendor::qti::hardware::bluetooth_audio::V2_0::AacObjectType;
    using HidlAacVariableBitRate =
            ::vendor::qti::hardware::bluetooth_audio::V2_0::AacVariableBitRate;

    HidlAacParameters parameters;
    parameters.sampleRate = ToHidlSampleRate(aac_config.sampleRateHz);
    parameters.bitsPerSample = ToHidlBitsPerSample(aac_config.bitsPerSample);
    parameters.channelMode = ToHidlChannelMode(aac_config.channelMode);
    parameters.variableBitRateEnabled = aac_config.variableBitRateEnabled
                                                ? HidlAacVariableBitRate::ENABLED
                                                : HidlAacVariableBitRate::DISABLED;
    switch (aac_config.objectType) {
        case AidlAacObjectType::MPEG2_LC:
            parameters.objectType = HidlAacObjectType::MPEG2_LC;
            break;
        case AidlAacObjectType::MPEG4_LC:
            parameters.objectType = HidlAacObjectType::MPEG4_LC;
            break;
        case AidlAacObjectType::MPEG4_LTP:
            parameters.objectType = HidlAacObjectType::MPEG4_LTP;
            break;
        case AidlAacObjectType::MPEG4_SCALABLE:
            parameters.objectType = HidlAacObjectType::MPEG4_SCALABLE;
            break;
        default:
            parameters.objectType = HidlAacObjectType::MPEG2_LC;
            break;
    }
    return parameters;
}

HidlLdacParameters ToHidlLdacParameters(const AidlLdacConfiguration& ldac_config) {
    using AidlLdacChannelMode = ::aidl::android::hardware::bluetooth::audio::LdacChannelMode;
    using AidlLdacQualityIndex = ::aidl::android::hardware::bluetooth::audio::LdacQualityIndex;
    using HidlLdacChannelMode = ::vendor::qti::hardware::bluetooth_audio::V2_0::LdacChannelMode;
    using HidlLdacQualityIndex =
            ::vendor::qti::hardware::bluetooth_audio::V2_0::LdacQualityIndex;

    HidlLdacParameters parameters;
    parameters.sampleRate = ToHidlSampleRate(ldac_config.sampleRateHz);
    parameters.bitsPerSample = ToHidlBitsPerSample(ldac_config.bitsPerSample);
    switch (ldac_config.channelMode) {
        case AidlLdacChannelMode::STEREO:
            parameters.channelMode = HidlLdacChannelMode::STEREO;
            break;
        case AidlLdacChannelMode::DUAL:
            parameters.channelMode = HidlLdacChannelMode::DUAL;
            break;
        case AidlLdacChannelMode::MONO:
            parameters.channelMode = HidlLdacChannelMode::MONO;
            break;
        default:
            parameters.channelMode = HidlLdacChannelMode::UNKNOWN;
            break;
    }
    switch (ldac_config.qualityIndex) {
        case AidlLdacQualityIndex::HIGH:
            parameters.qualityIndex = HidlLdacQualityIndex::QUALITY_HIGH;
            break;
        case AidlLdacQualityIndex::MID:
            parameters.qualityIndex = HidlLdacQualityIndex::QUALITY_MID;
            break;
        case AidlLdacQualityIndex::LOW:
            parameters.qualityIndex = HidlLdacQualityIndex::QUALITY_LOW;
            break;
        default:
            parameters.qualityIndex = HidlLdacQualityIndex::QUALITY_ABR;
            break;
    }
    return parameters;
}

HidlAptxParameters ToHidlAptxParameters(const AidlAptxConfiguration& aptx_config) {
    HidlAptxParameters parameters;
    parameters.sampleRate = ToHidlSampleRate(aptx_config.sampleRateHz);
    parameters.bitsPerSample = ToHidlBitsPerSample(aptx_config.bitsPerSample);
    parameters.channelMode = ToHidlChannelMode(aptx_config.channelMode);
    return parameters;
}

bool ToHidlCodecConfiguration(const AidlCodecConfiguration& codec_config,
                              HidlCodecConfiguration* configuration) {
    configuration->codecType = ToHidlCodecType(codec_config.codecType);
    configuration->encodedAudioBitrate = static_cast<uint32_t>(codec_config.encodedAudioBitrate);
    configuration->peerMtu = static_cast<uint16_t>(codec_config.peerMtu);
    configuration->isScmstEnabled = codec_config.isScmstEnabled;
    switch (codec_config.config.getTag()) {
        case AidlCodecConfiguration::CodecSpecific::sbcConfig:
            configuration->config.sbcConfig = ToHidlSbcParameters(
                    codec_config.config.get<AidlCodecConfiguration::CodecSpecific::sbcConfig>());
            return true;
        case AidlCodecConfiguration::CodecSpecific::aacConfig:
            configuration->config.aacConfig = ToHidlAacParameters(
                    codec_config.config.get<AidlCodecConfiguration::CodecSpecific::aacConfig>());
            return true;
        case AidlCodecConfiguration::CodecSpecific::ldacConfig:
            configuration->config.ldacConfig = ToHidlLdacParameters(
                    codec_config.config.get<AidlCodecConfiguration::CodecSpecific::ldacConfig>());
            return true;
        case AidlCodecConfiguration::CodecSpecific::aptxConfig:
            configuration->config.aptxConfig = ToHidlAptxParameters(
                    codec_config.config.get<AidlCodecConfiguration::CodecSpecific::aptxConfig>());
            return true;
        default:
            return false;
    }
}

::aidl::android::hardware::bluetooth::audio::SbcCapabilities ToAidlSbcCapabilities(
        const HidlSbcParameters& parameters) {
    using AidlSbcAllocMethod = ::aidl::android::hardware::bluetooth::audio::SbcAllocMethod;
    using AidlSbcChannelMode = ::aidl::android::hardware::bluetooth::audio::SbcChannelMode;
    using HidlSbcAllocMethod = ::vendor::qti::hardware::bluetooth_audio::V2_0::SbcAllocMethod;
    using HidlSbcBlockLength = ::vendor::qti::hardware::bluetooth_audio::V2_0::SbcBlockLength;
    using HidlSbcChannelMode = ::vendor::qti::hardware::bluetooth_audio::V2_0::SbcChannelMode;
    using HidlSbcNumSubbands = ::vendor::qti::hardware::bluetooth_audio::V2_0::SbcNumSubbands;

    ::aidl::android::hardware::bluetooth::audio::SbcCapabilities capabilities;
    capabilities.sampleRateHz = ToAidlSampleRates(parameters.sampleRate);
    capabilities.bitsPerSample = ToAidlBitsPerSample(parameters.bitsPerSample);
    capabilities.minBitpool = parameters.minBitpool;
    capabilities.maxBitpool = parameters.maxBitpool;
    const std::pair<HidlSbcChannelMode, AidlSbcChannelMode> kChannelModes[] = {
            {HidlSbcChannelMode::JOINT_STEREO, AidlSbcChannelMode::JOINT_STEREO},
            {HidlSbcChannelMode::STEREO, AidlSbcChannelMode::STEREO},
            {HidlSbcChannelMode::DUAL, AidlSbcChannelMode::DUAL},
            {HidlSbcChannelMode::MONO, AidlSbcChannelMode::MONO},
    };
    for (const auto& [bit, mode] : kChannelModes) {
        if (static_cast<uint32_t>(parameters.channelMode) & static_cast<uint32_t>(bit)) {
            capabilities.channelMode.push_back(mode);
        }
    }
    const std::pair<HidlSbcBlockLength, int8_t> kBlockLengths[] = {
            {HidlSbcBlockLength::BLOCKS_4, 4},
            {HidlSbcBlockLength::BLOCKS_8, 8},
            {HidlSbcBlockLength::BLOCKS_12, 12},
            {HidlSbcBlockLength::BLOCKS_16, 16},
    };
    for (const auto& [bit, length] : kBlockLengths) {
        if (static_cast<uint32_t>(parameters.blockLength) & static_cast<uint32_t>(bit)) {
            capabilities.blockLength.push_back(length);
        }
    }
    const std::pair<HidlSbcNumSubbands, int8_t> kSubbands[] = {
            {HidlSbcNumSubbands::SUBBAND_4, 4},
            {HidlSbcNumSubbands::SUBBAND_8, 8},
    };
    for (const auto& [bit, subbands] : kSubbands) {
        if (static_cast<uint32_t>(parameters.numSubbands) & static_cast<uint32_t>(bit)) {
            capabilities.numSubbands.push_back(subbands);
        }
    }
    if (static_cast<uint32_t>(parameters.allocMethod) &
        static_cast<uint32_t>(HidlSbcAllocMethod::ALLOC_MD_S)) {
        capabilities.allocMethod.push_back(AidlSbcAllocMethod::ALLOC_MD_S);
    }
    if (static_cast<uint32_t>(parameters.allocMethod) &
        static_cast<uint32_t>(HidlSbcAllocMethod::ALLOC_MD_L)) {
        capabilities.allocMethod.push_back(AidlSbcAllocMethod::ALLOC_MD_L);
    }
    return capabilities;
}

::aidl::android::hardware::bluetooth::audio::AacCapabilities ToAidlAacCapabilities(
        const HidlAacParameters& parameters) {
    using AidlAacObjectType = ::aidl::android::hardware::bluetooth::audio::AacObjectType;
    using HidlAacObjectType = ::vendor::qti::hardware::bluetooth_audio::V2_0::AacObjectType;
    using HidlAacVariableBitRate =
            ::vendor::qti::hardware::bluetooth_audio::V2_0::AacVariableBitRate;

    ::aidl::android::hardware::bluetooth::audio::AacCapabilities capabilities;
    capabilities.sampleRateHz = ToAidlSampleRates(parameters.sampleRate);
    capabilities.channelMode = ToAidlChannelModes(parameters.channelMode);
    capabilities.bitsPerSample = ToAidlBitsPerSample(parameters.bitsPerSample);
    capabilities.variableBitRateSupported =
            static_cast<uint32_t>(parameters.variableBitRateEnabled) &
            static_cast<uint32_t>(HidlAacVariableBitRate::ENABLED);
    const std::pair<HidlAacObjectType, AidlAacObjectType> kObjectTypes[] = {
            {HidlAacObjectType::MPEG2_LC, AidlAacObjectType::MPEG2_LC},
            {HidlAacObjectType::MPEG4_LC, AidlAacObjectType::MPEG4_LC},
            {HidlAacObjectType::MPEG4_LTP, AidlAacObjectType::MPEG4_LTP},
            {HidlAacObjectType::MPEG4_SCALABLE, AidlAacObjectType::MPEG4_SCALABLE},
    };
    for (const auto& [bit, object_type] : kObjectTypes) {
        if (static_cast<uint32_t>(parameters.objectType) & static_cast<uint32_t>(bit)) {
            capabilities.objectType.push_back(object_type);
        }
    }
    return capabilities;
}

::aidl::android::hardware::bluetooth::audio::LdacCapabilities ToAidlLdacCapabilities(
        const HidlLdacParameters& parameters) {
    using AidlLdacChannelMode = ::aidl::android::hardware::bluetooth::audio::LdacChannelMode;
    using HidlLdacChannelMode = ::vendor::qti::hardware::bluetooth_audio::V2_0::LdacChannelMode;

    ::aidl::android::hardware::bluetooth::audio::LdacCapabilities capabilities;
    capabilities.sampleRateHz = ToAidlSampleRates(parameters.sampleRate);
    capabilities.bitsPerSample = ToAidlBitsPerSample(parameters.bitsPerSample);
    // The vendor interface carries no quality bitfield: every quality is legal.
    capabilities.qualityIndex = {
            ::aidl::android::hardware::bluetooth::audio::LdacQualityIndex::HIGH,
            ::aidl::android::hardware::bluetooth::audio::LdacQualityIndex::MID,
            ::aidl::android::hardware::bluetooth::audio::LdacQualityIndex::LOW,
            ::aidl::android::hardware::bluetooth::audio::LdacQualityIndex::ABR,
    };
    const std::pair<HidlLdacChannelMode, AidlLdacChannelMode> kChannelModes[] = {
            {HidlLdacChannelMode::STEREO, AidlLdacChannelMode::STEREO},
            {HidlLdacChannelMode::DUAL, AidlLdacChannelMode::DUAL},
            {HidlLdacChannelMode::MONO, AidlLdacChannelMode::MONO},
    };
    for (const auto& [bit, mode] : kChannelModes) {
        if (static_cast<uint32_t>(parameters.channelMode) & static_cast<uint32_t>(bit)) {
            capabilities.channelMode.push_back(mode);
        }
    }
    return capabilities;
}

::aidl::android::hardware::bluetooth::audio::AptxCapabilities ToAidlAptxCapabilities(
        const HidlAptxParameters& parameters) {
    ::aidl::android::hardware::bluetooth::audio::AptxCapabilities capabilities;
    capabilities.sampleRateHz = ToAidlSampleRates(parameters.sampleRate);
    capabilities.channelMode = ToAidlChannelModes(parameters.channelMode);
    capabilities.bitsPerSample = ToAidlBitsPerSample(parameters.bitsPerSample);
    return capabilities;
}

::aidl::android::hardware::bluetooth::audio::CodecCapabilities ToAidlCodecCapabilities(
        const HidlCodecCapabilities& codec) {
    ::aidl::android::hardware::bluetooth::audio::CodecCapabilities capabilities;
    capabilities.codecType = ToAidlCodecType(codec.codecType);
    switch (codec.codecType) {
        case HidlCodecType::SBC:
            capabilities.capabilities.set<::aidl::android::hardware::bluetooth::audio::
                                                   CodecCapabilities::Capabilities::sbcCapabilities>(
                    ToAidlSbcCapabilities(codec.capabilities.sbcCapabilities));
            break;
        case HidlCodecType::AAC:
            capabilities.capabilities.set<::aidl::android::hardware::bluetooth::audio::
                                                   CodecCapabilities::Capabilities::aacCapabilities>(
                    ToAidlAacCapabilities(codec.capabilities.aacCapabilities));
            break;
        case HidlCodecType::LDAC:
            capabilities.capabilities.set<::aidl::android::hardware::bluetooth::audio::
                                                   CodecCapabilities::Capabilities::ldacCapabilities>(
                    ToAidlLdacCapabilities(codec.capabilities.ldacCapabilities));
            break;
        case HidlCodecType::APTX:
        case HidlCodecType::APTX_HD:
            capabilities.capabilities.set<::aidl::android::hardware::bluetooth::audio::
                                                   CodecCapabilities::Capabilities::aptxCapabilities>(
                    ToAidlAptxCapabilities(codec.capabilities.aptxCapabilities));
            break;
        default:
            LOG(WARNING) << __func__ << ": unsupported codec capabilities, type="
                         << static_cast<uint32_t>(codec.codecType);
            break;
    }
    return capabilities;
}

}  // namespace

bool QtiAudioConfigConverter::IsQtiSessionType(const AidlSessionType& session_type) {
    switch (session_type) {
        case AidlSessionType::A2DP_SOFTWARE_ENCODING_DATAPATH:
        case AidlSessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH:
        case AidlSessionType::HEARING_AID_SOFTWARE_ENCODING_DATAPATH:
            return true;
        default:
            return false;
    }
}

QtiAudioConfigConverter::HidlSessionType QtiAudioConfigConverter::ToHidlSessionType(
        const AidlSessionType& session_type) {
    switch (session_type) {
        case AidlSessionType::A2DP_SOFTWARE_ENCODING_DATAPATH:
            return HidlSessionType::A2DP_SOFTWARE_ENCODING_DATAPATH;
        case AidlSessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH:
            return HidlSessionType::A2DP_HARDWARE_OFFLOAD_DATAPATH;
        case AidlSessionType::HEARING_AID_SOFTWARE_ENCODING_DATAPATH:
            return HidlSessionType::HEARING_AID_SOFTWARE_ENCODING_DATAPATH;
        default:
            return HidlSessionType::UNKNOWN;
    }
}

QtiAudioConfigConverter::HidlStatus QtiAudioConfigConverter::ToHidlStatus(
        const AidlStatus& status) {
    switch (status) {
        case AidlStatus::SUCCESS:
            return HidlStatus::SUCCESS;
        case AidlStatus::UNSUPPORTED_CODEC_CONFIGURATION:
            return HidlStatus::UNSUPPORTED_CODEC_CONFIGURATION;
        default:
            return HidlStatus::FAILURE;
    }
}

bool QtiAudioConfigConverter::ToHidlAudioConfig(const AidlAudioConfig& audio_config,
                                                HidlAudioConfig* hidl_audio_config) {
    switch (audio_config.getTag()) {
        case AidlAudioConfig::pcmConfig:
            hidl_audio_config->pcmConfig =
                    ToHidlPcmParameters(audio_config.get<AidlAudioConfig::pcmConfig>());
            return true;
        case AidlAudioConfig::a2dpConfig:
            return ToHidlCodecConfiguration(audio_config.get<AidlAudioConfig::a2dpConfig>(),
                                            &hidl_audio_config->codecConfig);
        default:
            LOG(WARNING) << __func__ << ": unsupported audio configuration tag "
                         << static_cast<int32_t>(audio_config.getTag());
            return false;
    }
}

QtiAudioConfigConverter::AidlAudioCapabilities QtiAudioConfigConverter::ToAidlAudioCapabilities(
        const AidlSessionType& session_type, const HidlAudioCapabilities& capabilities) {
    AidlAudioCapabilities aidl_capabilities;
    switch (session_type) {
        case AidlSessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH:
        case AidlSessionType::A2DP_HARDWARE_OFFLOAD_DECODING_DATAPATH:
            // The vendor union carries no discriminator; the session type says
            // the codec member is the valid one.
            aidl_capabilities.set<AidlAudioCapabilities::a2dpCapabilities>(
                    ToAidlCodecCapabilities(capabilities.codecCapabilities));
            break;
        default: {
            // Every other session the vendor provider serves carries PCM.
            const HidlPcmParameters& pcm = capabilities.pcmCapabilities;
            ::aidl::android::hardware::bluetooth::audio::PcmCapabilities aidl_pcm;
            aidl_pcm.sampleRateHz = ToAidlSampleRates(pcm.sampleRate);
            aidl_pcm.channelMode = ToAidlChannelModes(pcm.channelMode);
            aidl_pcm.bitsPerSample = ToAidlBitsPerSample(pcm.bitsPerSample);
            aidl_pcm.dataIntervalUs = {20000};
            aidl_capabilities.set<AidlAudioCapabilities::pcmCapabilities>(aidl_pcm);
            break;
        }
    }
    return aidl_capabilities;
}

}  // namespace bluetooth_audio
}  // namespace oneplus
}  // namespace device
