/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "brave/components/brave_rewards/core/endpoint/promotion/get_available/get_available.h"

#include <string>
#include <utility>
#include <vector>

#include "base/test/task_environment.h"
#include "brave/components/brave_rewards/core/ledger.h"
#include "brave/components/brave_rewards/core/ledger_client_mock.h"
#include "brave/components/brave_rewards/core/ledger_impl_mock.h"
#include "net/http/http_status_code.h"
#include "testing/gtest/include/gtest/gtest.h"

// npm run test -- brave_unit_tests --filter=GetAvailableTest.*

using ::testing::_;

namespace ledger {
namespace endpoint {
namespace promotion {

class GetAvailableTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  MockLedgerImpl mock_ledger_impl_;
  GetAvailable available_{&mock_ledger_impl_};
};

TEST_F(GetAvailableTest, ServerOK) {
  ON_CALL(*mock_ledger_impl_.mock_rewards_service(), LoadURL(_, _))
      .WillByDefault(
          [](mojom::UrlRequestPtr request, LoadURLCallback callback) {
            auto response = mojom::UrlResponse::New();
            response->status_code = 200;
            response->url = request->url;
            response->body = R"({
             "promotions": [
               {
                 "id": "83b3b77b-e7c3-455b-adda-e476fa0656d2",
                 "createdAt": "2020-06-08T15:04:45.352584Z",
                 "expiresAt": "2020-10-08T15:04:45.352584Z",
                 "version": 5,
                 "suggestionsPerGrant": 120,
                 "approximateValue": "30",
                 "type": "ugp",
                 "available": true,
                 "platform": "desktop",
                 "publicKeys": [
                   "dvpysTSiJdZUPihius7pvGOfngRWfDiIbrowykgMi1I="
                 ],
                 "legacyClaimed": false
               }
             ]
            })";
            std::move(callback).Run(std::move(response));
          });

  available_.Request(
      "macos",
      base::BindOnce([](mojom::Result result,
                        std::vector<mojom::PromotionPtr> list,
                        const std::vector<std::string>& corrupted_promotions) {
        mojom::Promotion expected_promotion;
        expected_promotion.id = "83b3b77b-e7c3-455b-adda-e476fa0656d2";
        expected_promotion.created_at = 1591628685;
        expected_promotion.expires_at = 1602169485;
        expected_promotion.version = 5;
        expected_promotion.suggestions = 120;
        expected_promotion.approximate_value = 30.0;
        expected_promotion.type = mojom::PromotionType::UGP;
        expected_promotion.public_keys =
            "[\"dvpysTSiJdZUPihius7pvGOfngRWfDiIbrowykgMi1I=\"]";
        expected_promotion.legacy_claimed = false;

        EXPECT_EQ(result, mojom::Result::LEDGER_OK);
        EXPECT_TRUE(corrupted_promotions.empty());
        EXPECT_EQ(list.size(), 1ul);
        EXPECT_TRUE(expected_promotion.Equals(*list[0]));
      }));
}

TEST_F(GetAvailableTest, ServerError400) {
  ON_CALL(*mock_ledger_impl_.mock_rewards_service(), LoadURL(_, _))
      .WillByDefault(
          [](mojom::UrlRequestPtr request, LoadURLCallback callback) {
            auto response = mojom::UrlResponse::New();
            response->status_code = 400;
            response->url = request->url;
            response->body = "";
            std::move(callback).Run(std::move(response));
          });

  available_.Request(
      "macos",
      base::BindOnce([](const mojom::Result result,
                        std::vector<mojom::PromotionPtr> list,
                        const std::vector<std::string>& corrupted_promotions) {
        EXPECT_EQ(result, mojom::Result::LEDGER_ERROR);
        EXPECT_TRUE(list.empty());
        EXPECT_TRUE(corrupted_promotions.empty());
      }));
}

TEST_F(GetAvailableTest, ServerError404) {
  ON_CALL(*mock_ledger_impl_.mock_rewards_service(), LoadURL(_, _))
      .WillByDefault(
          [](mojom::UrlRequestPtr request, LoadURLCallback callback) {
            auto response = mojom::UrlResponse::New();
            response->status_code = 404;
            response->url = request->url;
            response->body = "";
            std::move(callback).Run(std::move(response));
          });

  available_.Request(
      "macos",
      base::BindOnce([](mojom::Result result,
                        std::vector<mojom::PromotionPtr> list,
                        const std::vector<std::string>& corrupted_promotions) {
        EXPECT_EQ(result, mojom::Result::NOT_FOUND);
        EXPECT_TRUE(list.empty());
        EXPECT_TRUE(corrupted_promotions.empty());
      }));
}

TEST_F(GetAvailableTest, ServerError500) {
  ON_CALL(*mock_ledger_impl_.mock_rewards_service(), LoadURL(_, _))
      .WillByDefault(
          [](mojom::UrlRequestPtr request, LoadURLCallback callback) {
            auto response = mojom::UrlResponse::New();
            response->status_code = 500;
            response->url = request->url;
            response->body = "";
            std::move(callback).Run(std::move(response));
          });

  available_.Request(
      "macos",
      base::BindOnce([](mojom::Result result,
                        std::vector<mojom::PromotionPtr> list,
                        const std::vector<std::string>& corrupted_promotions) {
        EXPECT_EQ(result, mojom::Result::LEDGER_ERROR);
        EXPECT_TRUE(list.empty());
        EXPECT_TRUE(corrupted_promotions.empty());
      }));
}

TEST_F(GetAvailableTest, ServerErrorRandom) {
  ON_CALL(*mock_ledger_impl_.mock_rewards_service(), LoadURL(_, _))
      .WillByDefault(
          [](mojom::UrlRequestPtr request, LoadURLCallback callback) {
            auto response = mojom::UrlResponse::New();
            response->status_code = 453;
            response->url = request->url;
            response->body = "";
            std::move(callback).Run(std::move(response));
          });

  available_.Request(
      "macos",
      base::BindOnce([](mojom::Result result,
                        std::vector<mojom::PromotionPtr> list,
                        const std::vector<std::string>& corrupted_promotions) {
        EXPECT_EQ(result, mojom::Result::LEDGER_ERROR);
        EXPECT_TRUE(list.empty());
        EXPECT_TRUE(corrupted_promotions.empty());
      }));
}

TEST_F(GetAvailableTest, ServerWrongResponse) {
  ON_CALL(*mock_ledger_impl_.mock_rewards_service(), LoadURL(_, _))
      .WillByDefault(
          [](mojom::UrlRequestPtr request, LoadURLCallback callback) {
            auto response = mojom::UrlResponse::New();
            response->status_code = 200;
            response->url = request->url;
            response->body = R"({
             "promotions": [
                {
                  "foo": 0
                }
              ]
            })";
            std::move(callback).Run(std::move(response));
          });

  available_.Request(
      "macos",
      base::BindOnce([](mojom::Result result,
                        std::vector<mojom::PromotionPtr> list,
                        const std::vector<std::string>& corrupted_promotions) {
        EXPECT_EQ(result, mojom::Result::CORRUPTED_DATA);
        EXPECT_TRUE(list.empty());
        EXPECT_TRUE(corrupted_promotions.empty());
      }));
}

}  // namespace promotion
}  // namespace endpoint
}  // namespace ledger
