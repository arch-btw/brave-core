/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_BRAVE_BROWSER_MAIN_PARTS_H_
#define BRAVE_BROWSER_BRAVE_BROWSER_MAIN_PARTS_H_

#include <memory>

#include "chrome/browser/chrome_browser_main.h"

class DayZeroBrowserUIExptManager;

class BraveBrowserMainParts : public ChromeBrowserMainParts {
 public:
  BraveBrowserMainParts(bool is_integration_test, StartupData* startup_data);

  BraveBrowserMainParts(const BraveBrowserMainParts&) = delete;
  BraveBrowserMainParts& operator=(const BraveBrowserMainParts&) = delete;
  ~BraveBrowserMainParts() override;

  int PreMainMessageLoopRun() override;
  void PreBrowserStart() override;
  void PostBrowserStart() override;
  void PreShutdown() override;
  void PreProfileInit() override;
  void PostProfileInit(Profile* profile, bool is_initial_profile) override;

 private:
  friend class ChromeBrowserMainExtraPartsTor;

  std::unique_ptr<DayZeroBrowserUIExptManager>
      day_zero_browser_ui_expt_manager_;
};

#endif  // BRAVE_BROWSER_BRAVE_BROWSER_MAIN_PARTS_H_
