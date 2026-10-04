/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/vendor/lineage/slider/BnAlertSlider.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace aidl::vendor::lineage::slider {

class AlertSlider : public BnAlertSlider {
  public:
    AlertSlider();
    ~AlertSlider() override;

    ndk::ScopedAStatus registerCallback(const std::shared_ptr<IAlertSliderCallback>& callback,
                                        int32_t* _aidl_return) override;
    ndk::ScopedAStatus getState(int32_t* _aidl_return) override;

  private:
    void ueventLoop();
    void handleUEvent(const char* buf, int len);

    std::atomic<int32_t> mState{0};
    std::mutex mLock;
    std::shared_ptr<IAlertSliderCallback> mCallback;
    std::thread mThread;
    std::atomic<bool> mStop{false};
};

}  // namespace aidl::vendor::lineage::slider
