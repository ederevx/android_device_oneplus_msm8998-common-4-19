/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "AlertSliderHAL"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "AlertSlider.h"

using aidl::vendor::lineage::slider::AlertSlider;

int main() {
    android::base::InitLogging(nullptr, android::base::KernelLogger);

    ABinderProcess_setThreadPoolMaxThreadCount(0);

    std::shared_ptr<AlertSlider> slider = ndk::SharedRefBase::make<AlertSlider>();
    const std::string instance = std::string() + AlertSlider::descriptor + "/default";
    binder_status_t status = AServiceManager_addService(slider->asBinder().get(), instance.c_str());
    CHECK_EQ(status, STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
