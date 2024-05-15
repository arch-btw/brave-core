/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_CHROMIUM_SRC_CONTENT_BROWSER_MEDIA_SESSION_MEDIA_SESSION_IMPL_H_
#define BRAVE_CHROMIUM_SRC_CONTENT_BROWSER_MEDIA_SESSION_MEDIA_SESSION_IMPL_H_

#include <optional>

#include "content/public/browser/media_player_id.h"
#include "services/media_session/public/cpp/media_position.h"

#define NotifyMediaSessionMetadataChange                                \
  NotifyMediaSessionMetadataChange_Unused();                            \
  std::optional<media_session::MediaPosition>                           \
  GetMediaPositionFromNormalPlayerIfPossible();                         \
  std::optional<MediaPlayerId> GetActiveMediaPlayerId() const override; \
  void NotifyMediaSessionMetadataChange

#include "src/content/browser/media/session/media_session_impl.h"  // IWYU pragma: export

#undef NotifyMediaSessionMetadataChange

#endif  // BRAVE_CHROMIUM_SRC_CONTENT_BROWSER_MEDIA_SESSION_MEDIA_SESSION_IMPL_H_
