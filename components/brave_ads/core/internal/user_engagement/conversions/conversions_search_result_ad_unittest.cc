/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <memory>

#include "base/time/time.h"
#include "brave/components/brave_ads/core/internal/ad_units/ad_test_util.h"
#include "brave/components/brave_ads/core/internal/common/unittest/unittest_base.h"
#include "brave/components/brave_ads/core/internal/creatives/conversions/creative_set_conversion_test_util.h"
#include "brave/components/brave_ads/core/internal/settings/settings_test_util.h"
#include "brave/components/brave_ads/core/internal/user_engagement/ad_events/ad_event_builder.h"
#include "brave/components/brave_ads/core/internal/user_engagement/ad_events/ad_event_test_util.h"
#include "brave/components/brave_ads/core/internal/user_engagement/conversions/conversion/conversion_info.h"  // IWYU pragma: keep
#include "brave/components/brave_ads/core/internal/user_engagement/conversions/conversions.h"
#include "brave/components/brave_ads/core/internal/user_engagement/conversions/conversions_observer_mock.h"
#include "brave/components/brave_ads/core/public/account/confirmations/confirmation_type.h"
#include "brave/components/brave_ads/core/public/ad_units/ad_info.h"
#include "url/gurl.h"

// npm run test -- brave_unit_tests --filter=BraveAds*

namespace brave_ads {

namespace {

constexpr char kMatchingUrlPattern[] = "https://foo.com/*";

std::vector<GURL> BuildRedirectChain() {
  return {GURL("https://foo.com/bar"), GURL("https://www.baz.com"),
          GURL("https://qux.com/quux/corge")};
}

}  // namespace

class BraveAdsSearchResultAdConversionsTest : public UnitTestBase {
 protected:
  void SetUp() override {
    UnitTestBase::SetUp();

    conversions_ = std::make_unique<Conversions>();
    conversions_->AddObserver(&conversions_observer_mock_);
  }

  void TearDown() override {
    conversions_->RemoveObserver(&conversions_observer_mock_);

    UnitTestBase::TearDown();
  }

  void RecordAdEventsAdvancingTheClockAfterEach(
      const AdInfo& ad,
      const std::vector<ConfirmationType>& confirmation_types) {
    for (const auto& confirmation_type : confirmation_types) {
      test::RecordAdEvent(ad, confirmation_type);

      AdvanceClockBy(base::Milliseconds(1));
    }
  }

  void VerifyConversionExpectation(const AdInfo& ad,
                                   const ConversionActionType action_type) {
    EXPECT_CALL(conversions_observer_mock_,
                OnDidConvertAd(/*conversion*/ ::testing::FieldsAre(
                    ad.type, ad.creative_instance_id, ad.creative_set_id,
                    ad.campaign_id, ad.advertiser_id, ad.segment, action_type,
                    /*verifiable*/ std::nullopt)));
  }

  std::unique_ptr<Conversions> conversions_;

  ::testing::StrictMock<ConversionsObserverMock> conversions_observer_mock_;
};

TEST_F(BraveAdsSearchResultAdConversionsTest,
       ConvertViewedAdIfOptedInToSearchResultAds) {
  // Arrange
  const AdInfo ad = test::BuildAd(AdType::kSearchResultAd,
                                  /*should_generate_random_uuids=*/false);
  test::BuildAndSaveCreativeSetConversion(ad.creative_set_id,
                                          kMatchingUrlPattern,
                                          /*observation_window=*/base::Days(3));
  RecordAdEventsAdvancingTheClockAfterEach(
      ad, {ConfirmationType::kServedImpression,
           ConfirmationType::kViewedImpression});

  // Act & Assert
  VerifyConversionExpectation(ad, ConversionActionType::kViewThrough);
  conversions_->MaybeConvert(BuildRedirectChain(), /*html=*/{});
}

TEST_F(BraveAdsSearchResultAdConversionsTest,
       DoNotConvertViewedAdIfOptedOutOfSearchResultAds) {
  // Arrange
  test::OptOutOfSearchResultAds();

  const AdInfo ad = test::BuildAd(AdType::kSearchResultAd,
                                  /*should_generate_random_uuids=*/false);
  test::BuildAndSaveCreativeSetConversion(ad.creative_set_id,
                                          kMatchingUrlPattern,
                                          /*observation_window=*/base::Days(3));
  RecordAdEventsAdvancingTheClockAfterEach(
      ad, {ConfirmationType::kServedImpression,
           ConfirmationType::kViewedImpression});

  // Act & Assert
  conversions_->MaybeConvert(BuildRedirectChain(), /*html=*/{});
}

TEST_F(BraveAdsSearchResultAdConversionsTest,
       DoNotConvertViewedAdIfOptedOutOfSearchResultAdsAndNonRewardsUser) {
  // Arrange
  test::DisableBraveRewards();
  test::OptOutOfSearchResultAds();

  const AdInfo ad = test::BuildAd(AdType::kSearchResultAd,
                                  /*should_generate_random_uuids=*/false);
  test::BuildAndSaveCreativeSetConversion(ad.creative_set_id,
                                          kMatchingUrlPattern,
                                          /*observation_window=*/base::Days(3));
  RecordAdEventsAdvancingTheClockAfterEach(ad, {});

  // Act & Assert
  conversions_->MaybeConvert(BuildRedirectChain(), /*html=*/{});
}

TEST_F(BraveAdsSearchResultAdConversionsTest,
       DoNotConvertViewedAdForNonRewardsUser) {
  // Arrange
  test::DisableBraveRewards();

  const AdInfo ad = test::BuildAd(AdType::kSearchResultAd,
                                  /*should_generate_random_uuids=*/false);
  test::BuildAndSaveCreativeSetConversion(ad.creative_set_id,
                                          kMatchingUrlPattern,
                                          /*observation_window=*/base::Days(3));
  RecordAdEventsAdvancingTheClockAfterEach(
      ad, {ConfirmationType::kServedImpression,
           ConfirmationType::kViewedImpression});

  // Act & Assert
  conversions_->MaybeConvert(BuildRedirectChain(), /*html=*/{});
}

TEST_F(BraveAdsSearchResultAdConversionsTest,
       ConvertClickedAdIfOptedInToSearchResultAds) {
  // Arrange
  const AdInfo ad = test::BuildAd(AdType::kSearchResultAd,
                                  /*should_generate_random_uuids=*/false);
  test::BuildAndSaveCreativeSetConversion(ad.creative_set_id,
                                          kMatchingUrlPattern,
                                          /*observation_window=*/base::Days(3));
  RecordAdEventsAdvancingTheClockAfterEach(
      ad, {ConfirmationType::kServedImpression,
           ConfirmationType::kViewedImpression, ConfirmationType::kClicked});

  // Act & Assert
  VerifyConversionExpectation(ad, ConversionActionType::kClickThrough);
  conversions_->MaybeConvert(BuildRedirectChain(), /*html=*/{});
}

TEST_F(BraveAdsSearchResultAdConversionsTest,
       DoNotConvertClickedAdIfOptedOutOfSearchResultAds) {
  // Arrange
  test::OptOutOfSearchResultAds();

  const AdInfo ad = test::BuildAd(AdType::kSearchResultAd,
                                  /*should_generate_random_uuids=*/false);
  test::BuildAndSaveCreativeSetConversion(ad.creative_set_id,
                                          kMatchingUrlPattern,
                                          /*observation_window=*/base::Days(3));
  RecordAdEventsAdvancingTheClockAfterEach(
      ad, {ConfirmationType::kServedImpression,
           ConfirmationType::kViewedImpression, ConfirmationType::kClicked});

  // Act & Assert
  conversions_->MaybeConvert(BuildRedirectChain(), /*html=*/{});
}

TEST_F(BraveAdsSearchResultAdConversionsTest,
       DoNotConvertClickedAdIfOptedOutOfSearchResultAdsAndNonRewardsUser) {
  // Arrange
  test::DisableBraveRewards();
  test::OptOutOfSearchResultAds();

  const AdInfo ad = test::BuildAd(AdType::kSearchResultAd,
                                  /*should_generate_random_uuids=*/false);
  test::BuildAndSaveCreativeSetConversion(ad.creative_set_id,
                                          kMatchingUrlPattern,
                                          /*observation_window=*/base::Days(3));
  RecordAdEventsAdvancingTheClockAfterEach(ad, {ConfirmationType::kClicked});

  // Act & Assert
  conversions_->MaybeConvert(BuildRedirectChain(), /*html=*/{});
}

TEST_F(BraveAdsSearchResultAdConversionsTest,
       ConvertClickedAdForNonRewardsUser) {
  // Arrange
  test::DisableBraveRewards();

  const AdInfo ad = test::BuildAd(AdType::kSearchResultAd,
                                  /*should_generate_random_uuids=*/false);
  test::BuildAndSaveCreativeSetConversion(ad.creative_set_id,
                                          kMatchingUrlPattern,
                                          /*observation_window=*/base::Days(3));
  RecordAdEventsAdvancingTheClockAfterEach(ad, {ConfirmationType::kClicked});

  // Act & Assert
  VerifyConversionExpectation(ad, ConversionActionType::kClickThrough);
  conversions_->MaybeConvert(BuildRedirectChain(), /*html=*/{});
}

}  // namespace brave_ads
