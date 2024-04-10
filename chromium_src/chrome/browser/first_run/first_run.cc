/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "chrome/browser/first_run/first_run.h"

#include "base/command_line.h"
#include "base/notreached.h"
#include "brave/browser/metrics/switches.h"
#include "build/build_config.h"
#include "chrome/common/channel_info.h"
#include "components/version_info/channel.h"

#define IsMetricsReportingOptIn IsMetricsReportingOptIn_ChromiumImpl
#include "src/chrome/browser/first_run/first_run.cc"
#undef IsMetricsReportingOptIn

namespace first_run {

bool IsMetricsReportingOptIn(version_info::Channel channel) {
  switch (channel) {
    case version_info::Channel::STABLE:
#if BUILDFLAG(IS_ANDROID)
      // Change both to false when android implements ask on first crash
      // dialog
      return false;
#else
      return true;
#endif
    case version_info::Channel::BETA:  // fall through
    case version_info::Channel::DEV:   // fall through
    case version_info::Channel::CANARY:
      return false;
    case version_info::Channel::UNKNOWN:
      return true;
    default:
      NOTREACHED();
      return true;
  }
}

bool IsMetricsReportingOptIn() {
  // override the default value
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(metrics::switches::kForceMetricsOptInEnabled)) {
    return true;
  } else if (command_line.HasSwitch(
                 metrics::switches::kForceMetricsOptInDisabled)) {
    return false;
  }

#if defined(OFFICIAL_BUILD)
  return IsMetricsReportingOptIn(chrome::GetChannel());
#else
  return true;
#endif
}

}  // namespace first_run
