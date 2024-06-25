/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "chrome/test/base/testing_profile.h"

#include "brave/browser/ephemeral_storage/ephemeral_storage_service_factory.h"
#include "brave/browser/search_engines/search_engine_tracker.h"
#include "brave/components/brave_ads/core/public/prefs/pref_registry.h"
#include "brave/components/brave_rewards/common/pref_registry.h"
#include "brave/components/brave_vpn/common/buildflags/buildflags.h"
#include "build/build_config.h"
#include "chrome/browser/prefs/browser_prefs.h"
#include "chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.h"
#include "components/pref_registry/pref_registry_syncable.h"

#if BUILDFLAG(ENABLE_BRAVE_VPN)
#include "brave/browser/brave_vpn/brave_vpn_service_factory.h"
#endif  // BUILDFLAG(ENABLE_BRAVE_VPN)

#if !BUILDFLAG(IS_ANDROID)
#include "brave/browser/ui/commands/accelerator_service_factory.h"
#endif  // !BUILDFLAG(IS_ANDROID)

#if defined(TOOLKIT_VIEWS)
#include "brave/browser/ui/sidebar/sidebar_service_factory.h"
#endif  // defined(TOOLKIT_VIEWS)

namespace {

class BraveBrowserMainExtraPartsProfiles
    : public ChromeBrowserMainExtraPartsProfiles {
 public:
  BraveBrowserMainExtraPartsProfiles() = default;
  BraveBrowserMainExtraPartsProfiles(
      const BraveBrowserMainExtraPartsProfiles&) = delete;
  BraveBrowserMainExtraPartsProfiles& operator=(
      const BraveBrowserMainExtraPartsProfiles&) = delete;
  ~BraveBrowserMainExtraPartsProfiles() override = default;

  static void EnsureBrowserContextKeyedServiceFactoriesBuilt() {
    ChromeBrowserMainExtraPartsProfiles::
        EnsureBrowserContextKeyedServiceFactoriesBuilt();
    // Instead of calling brave::EnsureBrowserContextKeyedServiceFactoriesBuilt,
    // for now instantiate only the services that upstream tests absolutely need
    // because preferences those services register are accessed by the upstream
    // tests.
    EphemeralStorageServiceFactory::GetInstance();
    SearchEngineTrackerFactory::GetInstance();

#if BUILDFLAG(ENABLE_BRAVE_VPN)
    brave_vpn::BraveVpnServiceFactory::GetInstance();
#endif  // BUILDFLAG(ENABLE_BRAVE_VPN)

#if !BUILDFLAG(IS_ANDROID)
    commands::AcceleratorServiceFactory::GetInstance();
#endif  // !BUILDFLAG(IS_ANDROID)

#if defined(TOOLKIT_VIEWS)
    sidebar::SidebarServiceFactory::GetInstance();
#endif  // defined(TOOLKIT_VIEWS)
  }
};

}  // namespace

#define ChromeBrowserMainExtraPartsProfiles BraveBrowserMainExtraPartsProfiles
#include "src/chrome/test/base/testing_profile.cc"
#undef ChromeBrowserMainExtraPartsProfiles

#include "brave/test/base/brave_testing_profile.cc"
