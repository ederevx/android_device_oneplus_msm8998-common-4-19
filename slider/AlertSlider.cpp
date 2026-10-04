/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "AlertSliderHAL"

#include "AlertSlider.h"

#include <android-base/logging.h>
#include <cutils/uevent.h>
#include <string.h>
#include <unistd.h>

namespace aidl::vendor::lineage::slider {

AlertSlider::AlertSlider() {
    mThread = std::thread(&AlertSlider::ueventLoop, this);
}

AlertSlider::~AlertSlider() {
    mStop = true;
    if (mThread.joinable()) {
        mThread.join();
    }
}

ndk::ScopedAStatus AlertSlider::registerCallback(
        const std::shared_ptr<IAlertSliderCallback>& callback, int32_t* _aidl_return) {
    if (callback == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    std::lock_guard<std::mutex> lock(mLock);
    mCallback = callback;
    *_aidl_return = mState.load();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus AlertSlider::getState(int32_t* _aidl_return) {
    *_aidl_return = mState.load();
    return ndk::ScopedAStatus::ok();
}

void AlertSlider::ueventLoop() {
    int fd = uevent_open_socket(64 * 1024, true);
    if (fd < 0) {
        LOG(ERROR) << "uevent_open_socket failed";
        return;
    }
    std::string buffer(64 * 1024, '\0');
    while (!mStop) {
        int len = uevent_kernel_multicast_recv(fd, buffer.data(), buffer.size() - 1);
        if (len <= 0) {
            continue;
        }
        buffer[len] = '\0';
        handleUEvent(buffer.data(), len);
    }
    close(fd);
}

void AlertSlider::handleUEvent(const char* buf, int len) {
    const char* switchName = nullptr;
    const char* state = nullptr;
    for (const char* p = buf; p < buf + len && *p; p += strlen(p) + 1) {
        if (strncmp(p, "SWITCH_NAME=", 12) == 0) {
            switchName = p + 12;
        } else if (strncmp(p, "SWITCH_STATE=", 13) == 0) {
            state = p + 13;
        }
    }
    if (switchName == nullptr || state == nullptr) {
        return;
    }
    if (strcmp(switchName, "tri-state-key") != 0 && strcmp(switchName, "tri_state_key") != 0) {
        return;
    }
    int value = atoi(state);
    if (value < 1 || value > 3) {
        return;
    }
    mState = value;
    std::lock_guard<std::mutex> lock(mLock);
    if (mCallback != nullptr) {
        mCallback->onStateChanged(value);
    }
}

}  // namespace aidl::vendor::lineage::slider
