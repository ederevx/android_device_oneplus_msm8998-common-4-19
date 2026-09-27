/*
 * Copyright (C) 2025 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "AntiFlickerService"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

#include "AntiFlicker.h"

using ::android::base::ReadFileToString;
using ::android::base::Trim;
using ::android::base::WriteStringToFile;

namespace aidl {
namespace vendor {
namespace lineage {
namespace livedisplay {

static const std::string kDcPath = "/sys/devices/virtual/graphics/fb0/dc";

ndk::ScopedAStatus AntiFlicker::getEnabled(bool* _aidl_return) {
    std::string tmp;

    if (!ReadFileToString(kDcPath, &tmp, true)) {
        *_aidl_return = false;
        return ndk::ScopedAStatus::fromExceptionCode(EX_SERVICE_SPECIFIC);
    }

    *_aidl_return = std::stoi(Trim(tmp)) > 0;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus AntiFlicker::setEnabled(bool enabled) {
    if (!WriteStringToFile(std::to_string(enabled), kDcPath, true)) {
        LOG(ERROR) << "Failed to set anti flicker state";
        return ndk::ScopedAStatus::fromExceptionCode(EX_SERVICE_SPECIFIC);
    }

    return ndk::ScopedAStatus::ok();
}

}  // namespace livedisplay
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
