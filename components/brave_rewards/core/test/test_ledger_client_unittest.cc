/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_rewards/core/test/test_ledger_client.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "brave/components/brave_rewards/common/mojom/ledger.mojom.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ledger {

class TestRewardsServiceTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  TestRewardsService test_rewards_service_;
};

TEST_F(TestRewardsServiceTest, CanAccessDatabaseDirectly) {
  sql::Database* db =
      test_rewards_service_.database()->GetInternalDatabaseForTesting();
  ASSERT_TRUE(db->Execute("CREATE TABLE test_table (num INTEGER);"));
  ASSERT_TRUE(db->Execute("INSERT INTO test_table VALUES (42);"));
  sql::Statement s(db->GetUniqueStatement("SELECT num FROM test_table"));
  ASSERT_TRUE(s.Step());
  ASSERT_EQ(s.ColumnInt(0), 42);
}

TEST_F(TestRewardsServiceTest, LoadURLIsAsync) {
  auto request = mojom::UrlRequest::New();
  request->url = "https://brave.com";
  bool finished = false;
  test_rewards_service_.LoadURL(
      std::move(request),
      base::BindLambdaForTesting(
          [&finished](mojom::UrlResponsePtr) { finished = true; }));
  ASSERT_FALSE(finished);
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(finished);
}

}  // namespace ledger
