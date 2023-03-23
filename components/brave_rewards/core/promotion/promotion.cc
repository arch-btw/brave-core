/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_rewards/core/promotion/promotion.h"

#include <map>
#include <memory>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "brave/components/brave_rewards/core/common/time_util.h"
#include "brave/components/brave_rewards/core/constants.h"
#include "brave/components/brave_rewards/core/credentials/credentials_util.h"
#include "brave/components/brave_rewards/core/ledger_impl.h"
#include "brave/components/brave_rewards/core/legacy/wallet_info_properties.h"
#include "brave/components/brave_rewards/core/option_keys.h"
#include "brave/components/brave_rewards/core/promotion/promotion_transfer.h"
#include "brave/components/brave_rewards/core/promotion/promotion_util.h"

#include "wrapper.hpp"  // NOLINT

using std::placeholders::_1;

using challenge_bypass_ristretto::BatchDLEQProof;
using challenge_bypass_ristretto::BlindedToken;
using challenge_bypass_ristretto::PublicKey;
using challenge_bypass_ristretto::SignedToken;
using challenge_bypass_ristretto::UnblindedToken;

namespace ledger {
namespace promotion {

namespace {

const int kFetchPromotionsThresholdInSeconds =
    10 * base::Time::kSecondsPerMinute;

void HandleExpiredPromotions(
    LedgerImpl* ledger_impl,
    base::flat_map<std::string, mojom::PromotionPtr>* promotions) {
  DCHECK(promotions);
  if (!promotions) {
    return;
  }

  const uint64_t current_time = util::GetCurrentTimeStamp();

  for (auto& item : *promotions) {
    if (!item.second || item.second->status == mojom::PromotionStatus::OVER) {
      continue;
    }

    // we shouldn't expire ad grant
    if (item.second->type == mojom::PromotionType::ADS) {
      continue;
    }

    if (item.second->expires_at > 0 &&
        item.second->expires_at <= current_time) {
      ledger_impl->database()->UpdatePromotionStatus(
          item.second->id, mojom::PromotionStatus::OVER,
          [](const mojom::Result _) {});
    }
  }
}

}  // namespace

Promotion::Promotion(LedgerImpl* ledger)
    : attestation_(
          std::make_unique<ledger::attestation::AttestationImpl>(ledger)),
      transfer_(std::make_unique<PromotionTransfer>(ledger)),
      promotion_server_(std::make_unique<endpoint::PromotionServer>(ledger)),
      ledger_(ledger) {
  DCHECK(ledger_);
  credentials_ = credential::CredentialsFactory::Create(
      ledger_, mojom::CredsBatchType::PROMOTION);
  DCHECK(credentials_);
}

Promotion::~Promotion() = default;

void Promotion::Initialize() {
  if (!ledger_->state()->GetPromotionCorruptedMigrated()) {
    BLOG(1, "Migrating corrupted promotions");
    auto check_callback = std::bind(&Promotion::CheckForCorrupted, this, _1);

    ledger_->database()->GetAllPromotions(check_callback);
  }

  auto retry_callback = std::bind(&Promotion::Retry, this, _1);

  ledger_->database()->GetAllPromotions(retry_callback);
}

void Promotion::Fetch(ledger::FetchPromotionCallback callback) {
  // If we fetched promotions recently, fulfill this request from the
  // database instead of querying the server again
  if (!ledger::is_testing && _environment != mojom::Environment::STAGING) {
    const uint64_t last_promo_stamp =
        ledger_->state()->GetPromotionLastFetchStamp();
    const uint64_t now = util::GetCurrentTimeStamp();
    if (now - last_promo_stamp < kFetchPromotionsThresholdInSeconds) {
      auto all_callback =
          base::BindOnce(&Promotion::OnGetAllPromotionsFromDatabase,
                         base::Unretained(this), std::move(callback));

      ledger_->database()->GetAllPromotions(
          [callback = std::make_shared<decltype(all_callback)>(
               std::move(all_callback))](
              base::flat_map<std::string, mojom::PromotionPtr> promotions) {
            std::move(*callback).Run(std::move(promotions));
          });
      return;
    }
  }

  auto url_callback = base::BindOnce(
      &Promotion::OnFetch, base::Unretained(this), std::move(callback));

  auto client_info = ledger_->GetClientInfo();
  const std::string client = ParseClientInfoToString(std::move(client_info));
  promotion_server_->get_available()->Request(client, std::move(url_callback));
}

void Promotion::OnFetch(ledger::FetchPromotionCallback callback,
                        mojom::Result result,
                        std::vector<mojom::PromotionPtr> list,
                        const std::vector<std::string>& corrupted_promotions) {
  if (result == mojom::Result::NOT_FOUND) {
    ProcessFetchedPromotions(mojom::Result::NOT_FOUND, std::move(list),
                             std::move(callback));
    return;
  }

  if (result == mojom::Result::LEDGER_ERROR) {
    ProcessFetchedPromotions(mojom::Result::LEDGER_ERROR, std::move(list),
                             std::move(callback));
    return;
  }

  // even though that some promotions are corrupted
  // we should display non corrupted ones either way
  BLOG_IF(1, result == mojom::Result::CORRUPTED_DATA,
          "Promotions are not correct: " << base::JoinString(
              corrupted_promotions, ", "));

  auto all_callback =
      base::BindOnce(&Promotion::OnGetAllPromotions, base::Unretained(this),
                     std::move(callback), std::move(list));

  ledger_->database()->GetAllPromotions(
      [callback =
           std::make_shared<decltype(all_callback)>(std::move(all_callback))](
          base::flat_map<std::string, mojom::PromotionPtr> promotions) {
        std::move(*callback).Run(std::move(promotions));
      });
}

void Promotion::OnGetAllPromotions(
    ledger::FetchPromotionCallback callback,
    std::vector<mojom::PromotionPtr> list,
    base::flat_map<std::string, mojom::PromotionPtr> promotions) {
  HandleExpiredPromotions(ledger_, &promotions);

  std::vector<mojom::PromotionPtr> promotions_ui;
  for (const auto& item : list) {
    auto it = promotions.find(item->id);
    if (it != promotions.end()) {
      const auto status = it->second->status;
      promotions.erase(item->id);
      // Skip any promotions that are in the database and have been processed
      // in some way.
      if (status != mojom::PromotionStatus::ACTIVE &&
          status != mojom::PromotionStatus::OVER) {
        continue;
      }
    }

    // if the server return expiration for ads we need to set it to 0
    if (item->type == mojom::PromotionType::ADS) {
      item->expires_at = 0;
    }

    if (item->legacy_claimed) {
      item->status = mojom::PromotionStatus::ATTESTED;
      auto legacy_callback =
          std::bind(&Promotion::LegacyClaimedSaved, this, _1,
                    std::make_shared<mojom::PromotionPtr>(item->Clone()));
      ledger_->database()->SavePromotion(item->Clone(), legacy_callback);
      continue;
    }

    promotions_ui.push_back(item->Clone());

    ledger_->database()->SavePromotion(item->Clone(),
                                       [](const mojom::Result _) {});
  }

  // mark as over promotions that are in db with status active,
  // but are not available on the server anymore
  for (const auto& promotion : promotions) {
    if (promotion.second->status != mojom::PromotionStatus::ACTIVE) {
      continue;
    }

    bool found = std::any_of(
        list.begin(), list.end(),
        [&promotion](auto& item) { return item->id == promotion.second->id; });

    if (!found) {
      ledger_->database()->UpdatePromotionStatus(promotion.second->id,
                                                 mojom::PromotionStatus::OVER,
                                                 [](const mojom::Result) {});
    }
  }

  ProcessFetchedPromotions(mojom::Result::LEDGER_OK, std::move(promotions_ui),
                           std::move(callback));
}

void Promotion::OnGetAllPromotionsFromDatabase(
    ledger::FetchPromotionCallback callback,
    base::flat_map<std::string, mojom::PromotionPtr> promotions) {
  HandleExpiredPromotions(ledger_, &promotions);

  std::vector<mojom::PromotionPtr> promotions_ui;
  for (const auto& item : promotions) {
    if (item.second->status == mojom::PromotionStatus::ACTIVE) {
      promotions_ui.push_back(item.second->Clone());
    }
  }
  std::move(callback).Run(mojom::Result::LEDGER_OK, std::move(promotions_ui));
}

void Promotion::LegacyClaimedSaved(
    const mojom::Result result,
    std::shared_ptr<mojom::PromotionPtr> shared_promotion) {
  if (result != mojom::Result::LEDGER_OK) {
    BLOG(0, "Save failed");
    return;
  }

  GetCredentials(base::DoNothing(), std::move(*shared_promotion));
}

void Promotion::Claim(const std::string& promotion_id,
                      const std::string& payload,
                      ledger::ClaimPromotionCallback callback) {
  auto promotion_callback =
      base::BindOnce(&Promotion::OnClaimPromotion, base::Unretained(this),
                     std::move(callback), payload);

  ledger_->database()->GetPromotion(
      promotion_id,
      [callback = std::make_shared<decltype(promotion_callback)>(
           std::move(promotion_callback))](mojom::PromotionPtr promotion) {
        std::move(*callback).Run(std::move(promotion));
      });
}

void Promotion::OnClaimPromotion(ledger::ClaimPromotionCallback callback,
                                 const std::string& payload,
                                 mojom::PromotionPtr promotion) {
  if (!promotion) {
    BLOG(0, "Promotion is null");
    std::move(callback).Run(mojom::Result::LEDGER_ERROR, "");
    return;
  }

  if (promotion->status != mojom::PromotionStatus::ACTIVE) {
    BLOG(1, "Promotion already in progress");
    std::move(callback).Run(mojom::Result::IN_PROGRESS, "");
    return;
  }

  const auto wallet = ledger_->wallet()->GetWallet();
  if (!wallet) {
    BLOG(0, "Rewards wallet does not exist");
    std::move(callback).Run(mojom::Result::LEDGER_ERROR, "");
    return;
  }

  attestation_->Start(payload, std::move(callback));
}

void Promotion::Attest(const std::string& promotion_id,
                       const std::string& solution,
                       ledger::AttestPromotionCallback callback) {
  auto promotion_callback =
      base::BindOnce(&Promotion::OnAttestPromotion, base::Unretained(this),
                     std::move(callback), solution);

  ledger_->database()->GetPromotion(
      promotion_id,
      [callback = std::make_shared<decltype(promotion_callback)>(
           std::move(promotion_callback))](mojom::PromotionPtr promotion) {
        std::move(*callback).Run(std::move(promotion));
      });
}

void Promotion::OnAttestPromotion(ledger::AttestPromotionCallback callback,
                                  const std::string& solution,
                                  mojom::PromotionPtr promotion) {
  if (!promotion) {
    BLOG(1, "Promotion is null");
    std::move(callback).Run(mojom::Result::LEDGER_ERROR, nullptr);
    return;
  }

  if (promotion->status != mojom::PromotionStatus::ACTIVE) {
    BLOG(1, "Promotion already in progress");
    std::move(callback).Run(mojom::Result::IN_PROGRESS, nullptr);
    return;
  }

  auto confirm_callback =
      base::BindOnce(&Promotion::OnAttestedPromotion, base::Unretained(this),
                     std::move(callback), promotion->id);
  attestation_->Confirm(solution, std::move(confirm_callback));
}

void Promotion::OnAttestedPromotion(ledger::AttestPromotionCallback callback,
                                    const std::string& promotion_id,
                                    mojom::Result result) {
  if (result != mojom::Result::LEDGER_OK) {
    BLOG(0, "Attestation failed " << result);
    std::move(callback).Run(result, nullptr);
    return;
  }

  auto promotion_callback =
      base::BindOnce(&Promotion::OnCompletedAttestation, base::Unretained(this),
                     std::move(callback));

  ledger_->database()->GetPromotion(
      promotion_id,
      [callback = std::make_shared<decltype(promotion_callback)>(
           std::move(promotion_callback))](mojom::PromotionPtr promotion) {
        std::move(*callback).Run(std::move(promotion));
      });
}

void Promotion::OnCompletedAttestation(ledger::AttestPromotionCallback callback,
                                       mojom::PromotionPtr promotion) {
  if (!promotion) {
    BLOG(0, "Promotion does not exist");
    std::move(callback).Run(mojom::Result::LEDGER_ERROR, nullptr);
    return;
  }

  if (promotion->status == mojom::PromotionStatus::FINISHED) {
    BLOG(0, "Promotions already claimed");
    std::move(callback).Run(mojom::Result::GRANT_ALREADY_CLAIMED, nullptr);
    return;
  }

  promotion->status = mojom::PromotionStatus::ATTESTED;

  auto save_callback =
      base::BindOnce(&Promotion::AttestedSaved, base::Unretained(this),
                     std::move(callback), promotion->Clone());

  ledger_->database()->SavePromotion(
      std::move(promotion),
      [callback =
           std::make_shared<decltype(save_callback)>(std::move(save_callback))](
          mojom::Result result) { std::move(*callback).Run(result); });
}

void Promotion::AttestedSaved(ledger::AttestPromotionCallback callback,
                              mojom::PromotionPtr promotion,
                              mojom::Result result) {
  if (result != mojom::Result::LEDGER_OK) {
    BLOG(0, "Save failed ");
    std::move(callback).Run(result, nullptr);
    return;
  }

  auto claim_callback =
      base::BindOnce(&Promotion::Complete, base::Unretained(this),
                     std::move(callback), promotion->id);

  GetCredentials(std::move(claim_callback), std::move(promotion));
}

void Promotion::Complete(ledger::AttestPromotionCallback callback,
                         const std::string& promotion_id,
                         mojom::Result result) {
  auto promotion_callback =
      base::BindOnce(&Promotion::OnComplete, base::Unretained(this),
                     std::move(callback), result);

  ledger_->database()->GetPromotion(
      promotion_id,
      [callback = std::make_shared<decltype(promotion_callback)>(
           std::move(promotion_callback))](mojom::PromotionPtr promotion) {
        std::move(*callback).Run(std::move(promotion));
      });
}

void Promotion::OnComplete(ledger::AttestPromotionCallback callback,
                           mojom::Result result,
                           mojom::PromotionPtr promotion) {
  BLOG(1, "Promotion completed with result " << result);
  if (promotion && result == mojom::Result::LEDGER_OK) {
    ledger_->database()->SaveBalanceReportInfoItem(
        util::GetCurrentMonth(), util::GetCurrentYear(),
        ConvertPromotionTypeToReportType(promotion->type),
        promotion->approximate_value, [](mojom::Result) {});
  }

  std::move(callback).Run(result, std::move(promotion));
}

void Promotion::ProcessFetchedPromotions(
    const mojom::Result result,
    std::vector<mojom::PromotionPtr> promotions,
    ledger::FetchPromotionCallback callback) {
  const uint64_t now = util::GetCurrentTimeStamp();
  ledger_->state()->SetPromotionLastFetchStamp(now);
  last_check_timer_.Stop();
  const bool retry =
      result != mojom::Result::LEDGER_OK && result != mojom::Result::NOT_FOUND;
  Refresh(retry);
  std::move(callback).Run(result, std::move(promotions));
}

void Promotion::GetCredentials(ledger::ResultCallback callback,
                               mojom::PromotionPtr promotion) {
  if (!promotion) {
    BLOG(0, "Promotion is null");
    std::move(callback).Run(mojom::Result::LEDGER_ERROR);
    return;
  }

  credential::CredentialsTrigger trigger;
  trigger.id = promotion->id;
  trigger.size = promotion->suggestions;
  trigger.type = mojom::CredsBatchType::PROMOTION;

  auto creds_callback =
      base::BindOnce(&Promotion::CredentialsProcessed, base::Unretained(this),
                     std::move(callback), promotion->id);

  credentials_->Start(trigger, std::move(creds_callback));
}

void Promotion::CredentialsProcessed(ledger::ResultCallback callback,
                                     const std::string& promotion_id,
                                     mojom::Result result) {
  if (result == mojom::Result::RETRY) {
    retry_timer_.Start(FROM_HERE, base::Seconds(5),
                       base::BindOnce(&Promotion::OnRetryTimerElapsed,
                                      base::Unretained(this)));
    std::move(callback).Run(mojom::Result::LEDGER_OK);
    return;
  }

  if (result == mojom::Result::NOT_FOUND) {
    ledger_->database()->UpdatePromotionStatus(
        promotion_id, mojom::PromotionStatus::OVER,
        [callback = std::make_shared<decltype(callback)>(std::move(callback))](
            mojom::Result result) { std::move(*callback).Run(result); });
    return;
  }

  if (result != mojom::Result::LEDGER_OK) {
    BLOG(0, "Credentials process not succeeded " << result);
    std::move(callback).Run(result);
    return;
  }

  ledger_->database()->UpdatePromotionStatus(
      promotion_id, mojom::PromotionStatus::FINISHED,
      [callback = std::make_shared<decltype(callback)>(std::move(callback))](
          mojom::Result result) { std::move(*callback).Run(result); });
}

void Promotion::Retry(
    base::flat_map<std::string, mojom::PromotionPtr> promotions) {
  HandleExpiredPromotions(ledger_, &promotions);

  for (auto& promotion : promotions) {
    if (!promotion.second) {
      continue;
    }

    switch (promotion.second->status) {
      case mojom::PromotionStatus::ATTESTED: {
        GetCredentials(base::DoNothing(), std::move(promotion.second));
        break;
      }
      case mojom::PromotionStatus::ACTIVE:
      case mojom::PromotionStatus::FINISHED:
      case mojom::PromotionStatus::CORRUPTED:
      case mojom::PromotionStatus::OVER: {
        break;
      }
    }
  }
}

void Promotion::Refresh(const bool retry_after_error) {
  if (last_check_timer_.IsRunning()) {
    return;
  }

  base::TimeDelta start_timer_in;

  if (retry_after_error) {
    start_timer_in = util::GetRandomizedDelay(base::Seconds(300));

    BLOG(1,
         "Failed to refresh promotion, will try again in " << start_timer_in);
  } else {
    const auto default_time = constant::kPromotionRefreshInterval;
    const uint64_t now = util::GetCurrentTimeStamp();
    const uint64_t last_promo_stamp =
        ledger_->state()->GetPromotionLastFetchStamp();

    uint64_t time_since_last_promo_check = 0ull;

    if (last_promo_stamp != 0ull && last_promo_stamp < now) {
      time_since_last_promo_check = now - last_promo_stamp;
    }

    if (now == last_promo_stamp) {
      start_timer_in = base::Seconds(default_time);
    } else if (time_since_last_promo_check > 0 &&
               default_time > time_since_last_promo_check) {
      start_timer_in =
          base::Seconds(default_time - time_since_last_promo_check);
    }
  }

  last_check_timer_.Start(FROM_HERE, start_timer_in,
                          base::BindOnce(&Promotion::OnLastCheckTimerElapsed,
                                         base::Unretained(this)));
}

void Promotion::CheckForCorrupted(
    const base::flat_map<std::string, mojom::PromotionPtr>& promotions) {
  if (promotions.empty()) {
    BLOG(1, "Promotion is empty");
    return;
  }

  std::vector<std::string> corrupted_promotions;

  for (const auto& item : promotions) {
    if (!item.second ||
        item.second->status != mojom::PromotionStatus::ATTESTED) {
      continue;
    }

    if (item.second->public_keys.empty() || item.second->public_keys == "[]") {
      corrupted_promotions.push_back(item.second->id);
    }
  }

  if (corrupted_promotions.empty()) {
    BLOG(1, "No corrupted promotions");
    CorruptedPromotionFixed(mojom::Result::LEDGER_OK);
    return;
  }

  auto get_callback = std::bind(&Promotion::CorruptedPromotionFixed, this, _1);

  ledger_->database()->UpdatePromotionsBlankPublicKey(corrupted_promotions,
                                                      get_callback);
}

void Promotion::CorruptedPromotionFixed(const mojom::Result result) {
  if (result != mojom::Result::LEDGER_OK) {
    BLOG(0, "Could not update public keys");
    return;
  }

  auto check_callback = std::bind(&Promotion::CheckForCorruptedCreds, this, _1);

  ledger_->database()->GetAllCredsBatches(check_callback);
}

void Promotion::CheckForCorruptedCreds(std::vector<mojom::CredsBatchPtr> list) {
  if (list.empty()) {
    BLOG(1, "Creds list is empty");
    ledger_->state()->SetPromotionCorruptedMigrated(true);
    return;
  }

  std::vector<std::string> corrupted_promotions;

  for (auto& item : list) {
    if (!item || (item->status != mojom::CredsBatchStatus::SIGNED &&
                  item->status != mojom::CredsBatchStatus::FINISHED)) {
      continue;
    }

    std::vector<std::string> unblinded_encoded_tokens;
    std::string error;
    bool result =
        credential::UnBlindCreds(*item, &unblinded_encoded_tokens, &error);

    if (!result) {
      BLOG(1, "Promotion corrupted " << item->trigger_id);
      corrupted_promotions.push_back(item->trigger_id);
    }
  }

  if (corrupted_promotions.empty()) {
    BLOG(1, "No corrupted creds");
    ledger_->state()->SetPromotionCorruptedMigrated(true);
    return;
  }

  auto get_callback = std::bind(&Promotion::CorruptedPromotions, this, _1,
                                corrupted_promotions);

  ledger_->database()->GetPromotionList(corrupted_promotions, get_callback);
}

void Promotion::CorruptedPromotions(std::vector<mojom::PromotionPtr> promotions,
                                    const std::vector<std::string>& ids) {
  base::Value::List corrupted_claims;

  for (auto& item : promotions) {
    if (!item) {
      continue;
    }

    corrupted_claims.Append(base::Value(item->claim_id));
  }

  if (corrupted_claims.empty()) {
    BLOG(1, "No corrupted creds");
    ledger_->state()->SetPromotionCorruptedMigrated(true);
    return;
  }

  auto url_callback = std::bind(&Promotion::OnCheckForCorrupted, this, _1, ids);

  promotion_server_->post_clobbered_claims()->Request(
      std::move(corrupted_claims), url_callback);
}

void Promotion::OnCheckForCorrupted(
    const mojom::Result result,
    const std::vector<std::string>& promotion_id_list) {
  if (result != mojom::Result::LEDGER_OK) {
    BLOG(0, "Failed to parse corrupted promotions response");
    return;
  }

  ledger_->state()->SetPromotionCorruptedMigrated(true);

  auto update_callback =
      std::bind(&Promotion::ErrorStatusSaved, this, _1, promotion_id_list);

  ledger_->database()->UpdatePromotionsStatus(
      promotion_id_list, mojom::PromotionStatus::CORRUPTED, update_callback);
}

void Promotion::ErrorStatusSaved(
    const mojom::Result result,
    const std::vector<std::string>& promotion_id_list) {
  // even if promotions fail, let's try to update at least creds
  if (result != mojom::Result::LEDGER_OK) {
    BLOG(0, "Promotion status save failed");
  }

  auto update_callback = std::bind(&Promotion::ErrorCredsStatusSaved, this, _1);

  ledger_->database()->UpdateCredsBatchesStatus(
      promotion_id_list, mojom::CredsBatchType::PROMOTION,
      mojom::CredsBatchStatus::CORRUPTED, update_callback);
}

void Promotion::ErrorCredsStatusSaved(const mojom::Result result) {
  if (result != mojom::Result::LEDGER_OK) {
    BLOG(0, "Creds status save failed");
  }

  // let's retry promotions that are valid now
  auto retry_callback = std::bind(&Promotion::Retry, this, _1);

  ledger_->database()->GetAllPromotions(retry_callback);
}

void Promotion::TransferTokens(ledger::PostSuggestionsClaimCallback callback) {
  transfer_->Start(std::move(callback));
}

void Promotion::OnRetryTimerElapsed() {
  ledger_->database()->GetAllPromotions(std::bind(&Promotion::Retry, this, _1));
}

void Promotion::OnLastCheckTimerElapsed() {
  Fetch(base::DoNothing());
}

void Promotion::GetDrainStatus(const std::string& drain_id,
                               ledger::GetDrainCallback callback) {
  promotion_server_->get_drain()->Request(drain_id, callback);
}

}  // namespace promotion
}  // namespace ledger
