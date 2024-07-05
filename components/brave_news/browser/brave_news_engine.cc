// Copyright (c) 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/components/brave_news/browser/brave_news_engine.h"

#include <memory>

#include "base/feature_list.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "brave/components/api_request_helper/api_request_helper.h"
#include "brave/components/brave_news/browser/channels_controller.h"
#include "brave/components/brave_news/browser/feed_controller.h"
#include "brave/components/brave_news/browser/feed_v2_builder.h"
#include "brave/components/brave_news/browser/network.h"
#include "brave/components/brave_news/browser/publishers_controller.h"
#include "brave/components/brave_news/browser/suggestions_controller.h"
#include "brave/components/brave_news/common/features.h"
#include "brave/components/brave_news/common/subscriptions_snapshot.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace brave_news {
BraveNewsEngine::BraveNewsEngine(
    std::unique_ptr<network::PendingSharedURLLoaderFactory>
        pending_shared_url_loader_factory)
    : url_loader_factory_(network::SharedURLLoaderFactory::Create(
          std::move(pending_shared_url_loader_factory))),
      api_request_helper_(GetNetworkTrafficAnnotationTag(),
                          url_loader_factory_),
      publishers_controller_(&api_request_helper_),
      channels_controller_(&publishers_controller_),
      suggestions_controller_(&publishers_controller_,
                              &api_request_helper_,
                              nullptr) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  sequence_checker_.EnableStackLogging();
}

BraveNewsEngine::~BraveNewsEngine() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void BraveNewsEngine::GetLocale(SubscriptionsSnapshot snapshot,
                                m::GetLocaleCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  publishers_controller_.GetLocale(snapshot, std::move(callback));
}

void BraveNewsEngine::GetSignals(SubscriptionsSnapshot snapshot,
                                 m::GetSignalsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto* builder = MaybeFeedV2Builder();
  CHECK(builder);

  builder->GetSignals(snapshot, std::move(callback));
}

void BraveNewsEngine::GetPublishers(SubscriptionsSnapshot snapshot,
                                    m::GetPublishersCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  publishers_controller_.GetOrFetchPublishers(snapshot, std::move(callback));
}

void BraveNewsEngine::EnsurePublishersIsUpdating(
    SubscriptionsSnapshot snapshot) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  publishers_controller_.EnsurePublishersIsUpdating(snapshot);
}

void BraveNewsEngine::EnsureFeedV2IsUpdating(SubscriptionsSnapshot snapshot) {
  auto* builder = MaybeFeedV2Builder();
  CHECK(builder);

  builder->EnsureFeedIsUpdating(snapshot);
}

void BraveNewsEngine::IsFeedV1UpdateAvailable(
    SubscriptionsSnapshot snapshot,
    const std::string& displayed_hash,
    m::IsFeedUpdateAvailableCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto* builder = MaybeFeedV1Builder();
  CHECK(builder);
  builder->DoesFeedVersionDiffer(snapshot, displayed_hash, std::move(callback));
}

void BraveNewsEngine::GetChannels(SubscriptionsSnapshot snapshot,
                                  m::GetChannelsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  channels_controller_.GetAllChannels(snapshot, std::move(callback));
}

void BraveNewsEngine::GetSuggestedPublisherIds(
    SubscriptionsSnapshot snapshot,
    m::GetSuggestedPublisherIdsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  suggestions_controller_.GetSuggestedPublisherIds(snapshot,
                                                   std::move(callback));
}

void BraveNewsEngine::GetFeed(SubscriptionsSnapshot snapshot,
                              m::GetFeedCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto* builder = MaybeFeedV1Builder();
  CHECK(builder);

  builder->GetOrFetchFeed(snapshot, std::move(callback));
}

void BraveNewsEngine::GetFeedV2(SubscriptionsSnapshot snapshot,
                                m::GetFeedV2Callback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto* builder = MaybeFeedV2Builder();
  CHECK(builder);

  builder->BuildAllFeed(snapshot, std::move(callback));
}

void BraveNewsEngine::GetFollowingFeed(SubscriptionsSnapshot snapshot,
                                       m::GetFollowingFeedCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto* builder = MaybeFeedV2Builder();
  CHECK(builder);

  builder->BuildFollowingFeed(snapshot, std::move(callback));
}

void BraveNewsEngine::GetChannelFeed(SubscriptionsSnapshot snapshot,
                                     const std::string& channel,
                                     m::GetPublisherFeedCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto* builder = MaybeFeedV2Builder();
  CHECK(builder);

  builder->BuildChannelFeed(snapshot, channel, std::move(callback));
}

void BraveNewsEngine::GetPublisherFeed(SubscriptionsSnapshot snapshot,
                                       const std::string& publisher_id,
                                       m::GetPublisherFeedCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto* builder = MaybeFeedV2Builder();
  CHECK(builder);

  builder->BuildPublisherFeed(snapshot, publisher_id, std::move(callback));
}

void BraveNewsEngine::CheckForFeedsUpdate(SubscriptionsSnapshot snapshot) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (MaybeFeedV2Builder()) {
    EnsureFeedV2IsUpdating(snapshot);
    return;
  }

  auto* builder = MaybeFeedV1Builder();
  CHECK(builder);

  builder->UpdateIfRemoteChanged(snapshot);
}

void BraveNewsEngine::PrefetchFeed(SubscriptionsSnapshot snapshot) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (auto* builder = MaybeFeedV2Builder()) {
    builder->BuildAllFeed(snapshot, base::DoNothing());
    return;
  }

  auto* builder = MaybeFeedV1Builder();
  CHECK(builder);
  builder->EnsureFeedIsCached(snapshot);
}

FeedV2Builder* BraveNewsEngine::MaybeFeedV2Builder() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!base::FeatureList::IsEnabled(features::kBraveNewsFeedUpdate)) {
    return nullptr;
  }

  if (!feed_v2_builder_) {
    feed_v2_builder_ = std::make_unique<FeedV2Builder>(
        publishers_controller_, channels_controller_, suggestions_controller_,
        nullptr, url_loader_factory_);
  }

  return feed_v2_builder_.get();
}

FeedController* BraveNewsEngine::MaybeFeedV1Builder() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (base::FeatureList::IsEnabled(features::kBraveNewsFeedUpdate)) {
    return nullptr;
  }

  if (!feed_controller_) {
    feed_controller_ = std::make_unique<FeedController>(
        &publishers_controller_, nullptr, url_loader_factory_);
  }

  return feed_controller_.get();
}

base::WeakPtr<BraveNewsEngine> BraveNewsEngine::GetWeakPtr() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return weak_ptr_factory_.GetWeakPtr();
}

}  // namespace brave_news
