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

// Based on hardware/interfaces/health/aidl/default/main.cpp, AOSP's example
// AIDL health implementation.
//
// The single difference is the charger callback: the example keeps
// ChargerCallback::ChargerEnableSuspend() at false, because a health AIDL HAL
// cannot read ro.charger.* properties. This device sets
// ro.charger.enable_suspend=true, so charger mode has to ask for autosuspend
// itself. Recovery builds have no charger UI (CHARGER_FORCE_NO_UI) and no
// ChargerUtils.cpp in libhealth_aidl_impl, hence the guard.

#include <android-base/logging.h>
#include <android/binder_interface_utils.h>
#include <health-impl/Health.h>
#include <health/utils.h>

#ifndef CHARGER_FORCE_NO_UI
#define CHARGER_FORCE_NO_UI 0
#endif

#if !CHARGER_FORCE_NO_UI
#include <health-impl/ChargerUtils.h>
#endif

using aidl::android::hardware::health::HalHealthLoop;
using aidl::android::hardware::health::Health;

#if !CHARGER_FORCE_NO_UI
namespace aidl::android::hardware::health::charger {

// Equivalent to ro.charger.enable_suspend=true in the HIDL charger.
class ChargerCallbackImpl : public ChargerCallback {
  public:
    using ChargerCallback::ChargerCallback;

    bool ChargerEnableSuspend() override { return true; }
};

}  // namespace aidl::android::hardware::health::charger
#endif

static constexpr const char* gInstanceName = "default";
static constexpr std::string_view gChargerArg{"--charger"};

int main(int argc, char** argv) {
#ifdef __ANDROID_RECOVERY__
    android::base::InitLogging(argv, android::base::KernelLogger);
#endif

    auto config = std::make_unique<healthd_config>();
    ::android::hardware::health::InitHealthdConfig(config.get());
    auto binder = ndk::SharedRefBase::make<Health>(gInstanceName, std::move(config));

    if (argc >= 2 && argv[1] == gChargerArg) {
#if !CHARGER_FORCE_NO_UI
        return aidl::android::hardware::health::charger::ChargerModeMain(
                binder,
                std::make_shared<aidl::android::hardware::health::charger::ChargerCallbackImpl>(
                        binder));
#else
        LOG(INFO) << "Starting charger mode without UI.";
#endif
    } else {
        LOG(INFO) << "Starting health HAL.";
    }

    auto hal_health_loop = std::make_shared<HalHealthLoop>(binder, binder);
    return hal_health_loop->StartLoop();
}
