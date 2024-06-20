// Copyright (c) 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_COMPONENTS_BRAVE_NEWS_BROWSER_BRAVE_NEWS_ENGINE_H_
#define BRAVE_COMPONENTS_BRAVE_NEWS_BROWSER_BRAVE_NEWS_ENGINE_H_

#include <memory>

#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/thread_annotations.h"
#include "brave/components/api_request_helper/api_request_helper.h"
#include "brave/components/brave_news/browser/channels_controller.h"
#include "brave/components/brave_news/browser/feed_controller.h"
#include "brave/components/brave_news/browser/feed_v2_builder.h"
#include "brave/components/brave_news/browser/publishers_controller.h"
#include "brave/components/brave_news/browser/suggestions_controller.h"
#include "brave/components/brave_news/common/brave_news.mojom.h"
#include "brave/components/brave_news/common/subscriptions_snapshot.h"
#include "components/history/core/browser/history_service.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace brave_news {

class BraveNewsEngine {
 public:
  // Alias so its easier to reuse the callbacks from the mojom interface.
  using m = mojom::BraveNewsController;

  explicit BraveNewsEngine(
      std::unique_ptr<network::PendingSharedURLLoaderFactory>
          pending_shared_url_loader_factory);
  BraveNewsEngine(const BraveNewsEngine&) = delete;
  BraveNewsEngine& operator=(const BraveNewsEngine&) = delete;
  ~BraveNewsEngine();

  void GetLocale(SubscriptionsSnapshot snapshot, m::GetLocaleCallback callback);
  void GetFeed(SubscriptionsSnapshot snapshot, m::GetFeedCallback callback);
  void GetFollowingFeed(SubscriptionsSnapshot snapshot,
                        m::GetFollowingFeedCallback callback);
  void GetChannelFeed(SubscriptionsSnapshot snapshot,
                      const std::string& channel,
                      m::GetChannelFeedCallback callback);
  void GetPublisherFeed(SubscriptionsSnapshot snapshot,
                        const std::string& publisher_id,
                        m::GetPublisherFeedCallback callback);
  void EnsureFeedV2IsUpdating();
  void GetFeedV2(SubscriptionsSnapshot snapshot, m::GetFeedV2Callback callback);

  void GetSignals(SubscriptionsSnapshot snapshot,
                  m::GetSignalsCallback callback);
  void GetPublishers(SubscriptionsSnapshot snapshot,
                     m::GetPublishersCallback callback);
  void GetChannels(SubscriptionsSnapshot snapshot,
                   m::GetChannelsCallback callback);

  void GetSuggestedPublisherIds(SubscriptionsSnapshot snapshot,
                                m::GetSuggestedPublisherIdsCallback callback);

 private:
  FeedV2Builder* MaybeFeedV2Builder();
  FeedController* MaybeFeedV1Builder();

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_
      GUARDED_BY_CONTEXT(sequence_checker_);
  api_request_helper::APIRequestHelper api_request_helper_
      GUARDED_BY_CONTEXT(sequence_checker_);

  PublishersController publishers_controller_
      GUARDED_BY_CONTEXT(sequence_checker_);
  ChannelsController channels_controller_ GUARDED_BY_CONTEXT(sequence_checker_);
  SuggestionsController suggestions_controller_
      GUARDED_BY_CONTEXT(sequence_checker_);

  std::unique_ptr<FeedController> feed_controller_
      GUARDED_BY_CONTEXT(sequence_checker_);

  std::unique_ptr<FeedV2Builder> feed_v2_builder_
      GUARDED_BY_CONTEXT(sequence_checker_);

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<BraveNewsEngine> weak_ptr_factory_{this};
};

}  // namespace brave_news

#endif  // BRAVE_COMPONENTS_BRAVE_NEWS_BROWSER_BRAVE_NEWS_ENGINE_H_
