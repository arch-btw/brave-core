/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_wallet/browser/solana_tx_manager.h"

#include <memory>
#include <optional>
#include <set>
#include <utility>

#include "base/base64.h"
#include "base/notreached.h"
#include "brave/components/brave_wallet/browser/account_resolver_delegate.h"
#include "brave/components/brave_wallet/browser/blockchain_registry.h"
#include "brave/components/brave_wallet/browser/brave_wallet_utils.h"
#include "brave/components/brave_wallet/browser/json_rpc_service.h"
#include "brave/components/brave_wallet/browser/solana_block_tracker.h"
#include "brave/components/brave_wallet/browser/solana_instruction_builder.h"
#include "brave/components/brave_wallet/browser/solana_keyring.h"
#include "brave/components/brave_wallet/browser/solana_message.h"
#include "brave/components/brave_wallet/browser/solana_message_header.h"
#include "brave/components/brave_wallet/browser/solana_tx_meta.h"
#include "brave/components/brave_wallet/browser/solana_tx_state_manager.h"
#include "brave/components/brave_wallet/common/brave_wallet.mojom.h"
#include "brave/components/brave_wallet/common/brave_wallet_constants.h"
#include "brave/components/brave_wallet/common/brave_wallet_types.h"
#include "brave/components/brave_wallet/common/solana_utils.h"
#include "components/grit/brave_components_strings.h"
#include "ui/base/l10n/l10n_util.h"

namespace brave_wallet {

constexpr int kValidBlockHeightThreshold = 150;
// The number of compute units required to modify the compute
// units and add a priority fee.
constexpr int kAddPriorityFeeComputeUnits = 300;
constexpr int kMininumFeePerComputeUnits = 1;

SolanaTxManager::SolanaTxManager(
    TxService* tx_service,
    JsonRpcService* json_rpc_service,
    KeyringService* keyring_service,
    PrefService* prefs,
    TxStorageDelegate* delegate,
    AccountResolverDelegate* account_resolver_delegate)
    : TxManager(
          std::make_unique<SolanaTxStateManager>(prefs,
                                                 delegate,
                                                 account_resolver_delegate),
          std::make_unique<SolanaBlockTracker>(json_rpc_service),
          tx_service,
          keyring_service,
          prefs),
      json_rpc_service_(json_rpc_service),
      weak_ptr_factory_(this) {
  GetSolanaBlockTracker()->AddObserver(this);
}

SolanaTxManager::~SolanaTxManager() {
  GetSolanaBlockTracker()->RemoveObserver(this);
}

void SolanaTxManager::AddUnapprovedTransaction(
    const std::string& chain_id,
    mojom::TxDataUnionPtr tx_data_union,
    const mojom::AccountIdPtr& from,
    const std::optional<url::Origin>& origin,
    AddUnapprovedTransactionCallback callback) {
  LOG(ERROR) << "SolanaTxManager::AddUnapprovedTransaction 0";
  DCHECK(tx_data_union->is_solana_tx_data());

  auto tx = SolanaTransaction::FromSolanaTxData(
      std::move(tx_data_union->get_solana_tx_data()));
  if (!tx) {
    std::move(callback).Run(
        false, "",
        l10n_util::GetStringUTF8(IDS_WALLET_SEND_TRANSACTION_CONVERT_TX_DATA));
    return;
  }

  LOG(ERROR) << "SolanaTxManager::AddUnapprovedTransaction 1";

  auto meta = std::make_unique<SolanaTxMeta>(from, std::move(tx));
  meta->set_id(TxMeta::GenerateMetaID());
  meta->set_origin(
      origin.value_or(url::Origin::Create(GURL("chrome://wallet"))));
  meta->set_created_time(base::Time::Now());
  meta->set_status(mojom::TransactionStatus::Unapproved);
  meta->set_chain_id(chain_id);

  LOG(ERROR) << "SolanaTxManager::AddUnapprovedTransaction 2";

  // If the transaction is partially signed, we can't modify the instructions
  // to add a priority fee.
  if (meta->tx()->IsPartialSigned()) {
    if (!tx_state_manager_->AddOrUpdateTx(*meta)) {
      std::move(callback).Run(
          false, "", l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
      return;
    }
    std::move(callback).Run(true, meta->id(), "");
    return;
  }

  LOG(ERROR) << "SolanaTxManager::AddUnapprovedTransaction 3";

  // Fetch the compute units used for priority fee estimation
  // by using the simulateTransaction API.
  auto internal_callback =
      base::BindOnce(&SolanaTxManager::ContinueAddUnapprovedTransaction,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback));

  LOG(ERROR) << "SolanaTxManager::AddUnapprovedTransaction 4";

  GetSolanaGasEstimationAndMeta(chain_id, std::move(meta),
                                std::move(internal_callback));
}

void SolanaTxManager::OnSimulateSolanaTransaction(
    const std::string& chain_id,
    std::unique_ptr<SolanaTxMeta> meta,
    uint64_t base_fee,
    GetSolanaGasEstimationAndMetaCallback callback,
    uint64_t compute_units_consumed,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run({}, {}, error, error_message);
    return;
  }

  // Fetch the fee estimate
  auto internal_callback =
      base::BindOnce(&SolanaTxManager::OnGetRecentSolanaPrioritizationFees,
                     weak_ptr_factory_.GetWeakPtr(), std::move(meta), base_fee,
                     compute_units_consumed, std::move(callback));
  json_rpc_service_->GetRecentSolanaPrioritizationFees(
      chain_id, std::move(internal_callback));
}

void SolanaTxManager::OnGetRecentSolanaPrioritizationFees(
    std::unique_ptr<SolanaTxMeta> meta,
    uint64_t base_fee,
    uint64_t compute_units,
    GetSolanaGasEstimationAndMetaCallback callback,
    std::vector<std::pair<uint64_t, uint64_t>> recent_fees,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  LOG(ERROR) << "SolanaTxManager::OnGetRecentSolanaPrioritizationFees 0";
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run({}, {}, error, error_message);
    return;
  }

  uint64_t median = 0;
  if (!recent_fees.empty()) {
    std::sort(recent_fees.begin(), recent_fees.end(),
              [](const std::pair<uint64_t, uint64_t>& a,
                 const std::pair<uint64_t, uint64_t>& b) {
                return a.second < b.second;
              });

    size_t size = recent_fees.size();
    if (size % 2 == 0) {
      median =
          (recent_fees[size / 2 - 1].second + recent_fees[size / 2].second) / 2;
    } else {
      median = recent_fees[size / 2].second;
    }
  }

  LOG(ERROR) << "SolanaTxManager::OnGetRecentSolanaPrioritizationFees 1";
  mojom::SolanaGasEstimationPtr estimation = mojom::SolanaGasEstimation::New();
  estimation->base_fee = base_fee;
  // The simulation was performed without the instructions that set a compute
  // budget and priority fee, so we must add those as well.
  estimation->compute_units = compute_units + kAddPriorityFeeComputeUnits;

  if (median == 0) {
    estimation->fee_per_compute_unit = kMininumFeePerComputeUnits;
  } else {
    estimation->fee_per_compute_unit = median;
  }

  LOG(ERROR) << "SolanaTxManager::OnGetRecentSolanaPrioritizationFees 2";

  std::move(callback).Run(std::move(meta), std::move(estimation),
                          mojom::SolanaProviderError::kSuccess, "");
}

void SolanaTxManager::OnGetEstimatedTxFeeAndMeta(
    const std::string& chain_id,
    GetSolanaGasEstimationAndMetaCallback callback,
    std::unique_ptr<SolanaTxMeta> meta,
    uint64_t base_fee,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run({}, {}, error, error_message);
    return;
  }

  const auto unsigned_tx = meta->tx()->GetUnsignedTransaction();
  auto internal_callback =
      base::BindOnce(&SolanaTxManager::OnSimulateSolanaTransaction,
                     weak_ptr_factory_.GetWeakPtr(), chain_id, std::move(meta),
                     base_fee, std::move(callback));
  json_rpc_service_->SimulateSolanaTransaction(chain_id, unsigned_tx,
                                               std::move(internal_callback));
}

void SolanaTxManager::FinishGetEstimatedTxFee(
    GetEstimatedTxFeeCallback callback,
    std::unique_ptr<SolanaTxMeta> meta,
    uint64_t base_fee,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  std::move(callback).Run(base_fee, error, error_message);
}

void SolanaTxManager::FinishGetSolanaGasEstimation(
    GetSolanaGasEstimationCallback callback,
    std::unique_ptr<SolanaTxMeta> meta,
    mojom::SolanaGasEstimationPtr estimation,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  std::move(callback).Run(std::move(estimation), error, error_message);
}

void SolanaTxManager::ContinueAddUnapprovedTransaction(
    AddUnapprovedTransactionCallback callback,
    std::unique_ptr<SolanaTxMeta> meta,
    mojom::SolanaGasEstimationPtr estimation,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  LOG(ERROR) << "SolanaTxManager::ContinueAddUnapprovedTransaction";
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run(false, "", error_message);
    return;
  }

  auto compute_units = estimation->compute_units;
  auto fee_per_compute_unit = estimation->fee_per_compute_unit;

  meta->tx()->set_gas_estimation(std::move(estimation));

  // Modify compute units and add the priority fee
  meta->tx()->message()->AddPriorityFee(compute_units, fee_per_compute_unit);
  if (!tx_state_manager_->AddOrUpdateTx(*meta)) {
    std::move(callback).Run(
        false, "", l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  std::move(callback).Run(true, meta->id(), "");
}

void SolanaTxManager::ApproveTransaction(const std::string& tx_meta_id,
                                         ApproveTransactionCallback callback) {
  std::unique_ptr<SolanaTxMeta> meta =
      GetSolanaTxStateManager()->GetSolanaTx(tx_meta_id);
  if (!meta) {
    DCHECK(false) << "Transaction should be found";
    std::move(callback).Run(
        false,
        mojom::ProviderErrorUnion::NewSolanaProviderError(
            mojom::SolanaProviderError::kInternalError),
        l10n_util::GetStringUTF8(IDS_BRAVE_WALLET_TRANSACTION_NOT_FOUND));
    return;
  }

  const std::string blockhash = meta->tx()->message()->recent_blockhash();
  auto chain_id = meta->chain_id();
  if (blockhash.empty()) {
    GetSolanaBlockTracker()->GetLatestBlockhash(
        chain_id,
        base::BindOnce(&SolanaTxManager::OnGetLatestBlockhash,
                       weak_ptr_factory_.GetWeakPtr(), std::move(meta),
                       std::move(callback)),
        true);
  } else {
    // No existing last valid block height info, use the current block height
    // + 150 as the last valid block height.
    json_rpc_service_->GetSolanaBlockHeight(
        chain_id,
        base::BindOnce(&SolanaTxManager::OnGetBlockHeightForBlockhash,
                       weak_ptr_factory_.GetWeakPtr(), std::move(meta),
                       std::move(callback), blockhash));
  }
}

void SolanaTxManager::OnGetBlockHeightForBlockhash(
    std::unique_ptr<SolanaTxMeta> meta,
    ApproveTransactionCallback callback,
    const std::string& blockhash,
    uint64_t block_height,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run(
        false, mojom::ProviderErrorUnion::NewSolanaProviderError(error),
        error_message);
    return;
  }

  OnGetLatestBlockhash(std::move(meta), std::move(callback), blockhash,
                       block_height + kValidBlockHeightThreshold,
                       mojom::SolanaProviderError::kSuccess, "");
}

void SolanaTxManager::OnGetLatestBlockhash(std::unique_ptr<SolanaTxMeta> meta,
                                           ApproveTransactionCallback callback,
                                           const std::string& latest_blockhash,
                                           uint64_t last_valid_block_height,
                                           mojom::SolanaProviderError error,
                                           const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run(
        false, mojom::ProviderErrorUnion::NewSolanaProviderError(error),
        error_message);
    return;
  }

  meta->set_status(mojom::TransactionStatus::Approved);
  meta->tx()->message()->set_recent_blockhash(latest_blockhash);
  meta->tx()->message()->set_last_valid_block_height(last_valid_block_height);
  if (!tx_state_manager_->AddOrUpdateTx(*meta)) {
    std::move(callback).Run(
        false,
        mojom::ProviderErrorUnion::NewSolanaProviderError(
            mojom::SolanaProviderError::kInternalError),
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  json_rpc_service_->SendSolanaTransaction(
      meta->chain_id(),
      meta->tx()->GetSignedTransaction(keyring_service_, meta->from()),
      meta->tx()->send_options(),
      base::BindOnce(&SolanaTxManager::OnSendSolanaTransaction,
                     weak_ptr_factory_.GetWeakPtr(), meta->id(),
                     std::move(callback)));
}

void SolanaTxManager::OnGetLatestBlockhashHardware(
    std::unique_ptr<SolanaTxMeta> meta,
    GetTransactionMessageToSignCallback callback,
    const std::string& latest_blockhash,
    uint64_t last_valid_block_height,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run(nullptr);
    return;
  }

  meta->tx()->message()->set_recent_blockhash(latest_blockhash);
  meta->tx()->message()->set_last_valid_block_height(last_valid_block_height);
  if (!tx_state_manager_->AddOrUpdateTx(*meta)) {
    std::move(callback).Run(nullptr);
    return;
  }

  auto message_signers_pair = meta->tx()->GetSerializedMessage();
  if (!message_signers_pair) {
    std::move(callback).Run(nullptr);
    return;
  }

  auto& message_bytes = message_signers_pair->first;
  std::move(callback).Run(
      mojom::MessageToSignUnion::NewMessageBytes(std::move(message_bytes)));
}

void SolanaTxManager::OnSendSolanaTransaction(
    const std::string& tx_meta_id,
    ApproveTransactionCallback callback,
    const std::string& tx_hash,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  std::unique_ptr<TxMeta> meta = tx_state_manager_->GetTx(tx_meta_id);
  if (!meta) {
    DCHECK(false) << "Transaction should be found";
    std::move(callback).Run(
        false,
        mojom::ProviderErrorUnion::NewSolanaProviderError(
            mojom::SolanaProviderError::kInternalError),
        l10n_util::GetStringUTF8(IDS_BRAVE_WALLET_TRANSACTION_NOT_FOUND));
    return;
  }

  bool success = error == mojom::SolanaProviderError::kSuccess;

  if (success) {
    meta->set_status(mojom::TransactionStatus::Submitted);
    meta->set_submitted_time(base::Time::Now());
    meta->set_tx_hash(tx_hash);
  } else {
    meta->set_status(mojom::TransactionStatus::Error);
  }

  if (!tx_state_manager_->AddOrUpdateTx(*meta)) {
    std::move(callback).Run(
        false,
        mojom::ProviderErrorUnion::NewSolanaProviderError(
            mojom::SolanaProviderError::kInternalError),
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  if (success) {
    UpdatePendingTransactions(meta->chain_id());
  }

  std::move(callback).Run(
      error_message.empty(),
      mojom::ProviderErrorUnion::NewSolanaProviderError(error), error_message);
}

void SolanaTxManager::UpdatePendingTransactions(
    const std::optional<std::string>& chain_id) {
  std::set<std::string> pending_chain_ids;
  if (chain_id.has_value()) {
    pending_chain_ids = pending_chain_ids_;
    pending_chain_ids.emplace(*chain_id);
    json_rpc_service_->GetSolanaBlockHeight(
        *chain_id, base::BindOnce(&SolanaTxManager::OnGetBlockHeight,
                                  weak_ptr_factory_.GetWeakPtr(), *chain_id));
  } else {
    auto pending_transactions = tx_state_manager_->GetTransactionsByStatus(
        std::nullopt, mojom::TransactionStatus::Submitted, std::nullopt);
    for (const auto& pending_transaction : pending_transactions) {
      const auto& pending_chain_id = pending_transaction->chain_id();
      // Skip already queried chain ids.
      if (pending_chain_ids.contains(pending_chain_id)) {
        continue;
      }

      json_rpc_service_->GetSolanaBlockHeight(
          pending_chain_id,
          base::BindOnce(&SolanaTxManager::OnGetBlockHeight,
                         weak_ptr_factory_.GetWeakPtr(), pending_chain_id));
      pending_chain_ids.emplace(pending_chain_id);
    }
  }
  CheckIfBlockTrackerShouldRun(pending_chain_ids);
}

void SolanaTxManager::OnGetBlockHeight(const std::string& chain_id,
                                       uint64_t block_height,
                                       mojom::SolanaProviderError error,
                                       const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    return;
  }

  auto pending_transactions = tx_state_manager_->GetTransactionsByStatus(
      chain_id, mojom::TransactionStatus::Submitted, std::nullopt);
  std::vector<std::string> tx_meta_ids;
  std::vector<std::string> tx_signatures;
  for (const auto& pending_transaction : pending_transactions) {
    tx_meta_ids.push_back(pending_transaction->id());
    tx_signatures.push_back(pending_transaction->tx_hash());
  }
  json_rpc_service_->GetSolanaSignatureStatuses(
      chain_id, tx_signatures,
      base::BindOnce(&SolanaTxManager::OnGetSignatureStatuses,
                     weak_ptr_factory_.GetWeakPtr(), chain_id, tx_meta_ids,
                     block_height));
}

void SolanaTxManager::OnGetSignatureStatuses(
    const std::string& chain_id,
    const std::vector<std::string>& tx_meta_ids,
    uint64_t block_height,
    const std::vector<std::optional<SolanaSignatureStatus>>& signature_statuses,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    return;
  }

  if (tx_meta_ids.size() != signature_statuses.size()) {
    return;
  }

  for (size_t i = 0; i < tx_meta_ids.size(); i++) {
    std::unique_ptr<SolanaTxMeta> meta =
        GetSolanaTxStateManager()->GetSolanaTx(tx_meta_ids[i]);
    if (!meta) {
      continue;
    }

    if (!signature_statuses[i]) {
      if (meta->tx()->message()->last_valid_block_height() &&
          meta->tx()->message()->last_valid_block_height() < block_height) {
        meta->set_status(mojom::TransactionStatus::Dropped);
        tx_state_manager_->AddOrUpdateTx(*meta);
      }
      continue;
    }

    if (!signature_statuses[i]->err.empty()) {
      meta->set_signature_status(*signature_statuses[i]);
      meta->set_status(mojom::TransactionStatus::Error);
      tx_state_manager_->AddOrUpdateTx(*meta);
      continue;
    }

    // Update SolanaTxMeta with signature status.
    if (!signature_statuses[i]->confirmation_status.empty()) {
      meta->set_signature_status(*signature_statuses[i]);

      if (signature_statuses[i]->confirmation_status == "finalized") {
        meta->set_status(mojom::TransactionStatus::Confirmed);
        meta->set_confirmed_time(base::Time::Now());
      }

      tx_state_manager_->AddOrUpdateTx(*meta);
    }
  }
}

void SolanaTxManager::SpeedupOrCancelTransaction(
    const std::string& tx_meta_id,
    bool cancel,
    SpeedupOrCancelTransactionCallback callback) {
  NOTIMPLEMENTED();
}

void SolanaTxManager::RetryTransaction(const std::string& tx_meta_id,
                                       RetryTransactionCallback callback) {
  std::unique_ptr<SolanaTxMeta> meta =
      GetSolanaTxStateManager()->GetSolanaTx(tx_meta_id);
  if (!meta || !meta->tx()) {
    std::move(callback).Run(
        false, "",
        l10n_util::GetStringUTF8(IDS_BRAVE_WALLET_TRANSACTION_NOT_FOUND));
    return;
  }

  if (!meta->IsRetriable()) {
    std::move(callback).Run(
        false, "",
        l10n_util::GetStringUTF8(IDS_BRAVE_WALLET_TRANSACTION_NOT_RETRIABLE));
    return;
  }

  if (!meta->tx()->message()->UsesDurableNonce()) {
    // Clear blockhash to trigger getting a new one when user approves.
    meta->tx()->message()->set_recent_blockhash("");

    // Clear sign_tx_param because they're no longer relevant for transactions
    // not using durable nonce, and clear this ensures we re-serialize the
    // message using the new blockhash in
    // SolanaTransacaction::GetSerializedMessage. sign_tx_param is not relevant
    // anymore because all existing signatures will be invalid if the blockhash
    // (message) changes, and we are the only one able to re-sign the new
    // message so we don't need to worry about having a different account order
    // than other implementations that dApp uses (Solana web3.js for example).
    meta->tx()->set_sign_tx_param(nullptr);
  }

  // Clear last valid block height for retried transaction, which will be
  // updated when user approves.
  meta->tx()->message()->set_last_valid_block_height(0);

  // Reset necessary fields for retried transaction.
  meta->set_id(TxMeta::GenerateMetaID());
  meta->set_status(mojom::TransactionStatus::Unapproved);
  meta->set_created_time(base::Time::Now());
  meta->set_submitted_time(base::Time());
  meta->set_confirmed_time(base::Time());
  meta->set_tx_hash("");
  meta->set_signature_status(SolanaSignatureStatus());

  meta->tx()->ClearRawSignatures();

  if (!tx_state_manager_->AddOrUpdateTx(*meta)) {
    std::move(callback).Run(
        false, "", l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  std::move(callback).Run(true, meta->id(), "");
}

void SolanaTxManager::GetTransactionMessageToSign(
    const std::string& tx_meta_id,
    GetTransactionMessageToSignCallback callback) {
  std::unique_ptr<SolanaTxMeta> meta =
      GetSolanaTxStateManager()->GetSolanaTx(tx_meta_id);
  if (!meta || !meta->tx()) {
    VLOG(1) << __FUNCTION__ << "No transaction found with id:" << tx_meta_id;
    std::move(callback).Run(nullptr);
    return;
  }

  const std::string blockhash = meta->tx()->message()->recent_blockhash();
  auto chain_id = meta->chain_id();
  if (blockhash.empty()) {
    GetSolanaBlockTracker()->GetLatestBlockhash(
        chain_id,
        base::BindOnce(&SolanaTxManager::OnGetLatestBlockhashHardware,
                       weak_ptr_factory_.GetWeakPtr(), std::move(meta),
                       std::move(callback)),
        true);
  } else {
    json_rpc_service_->GetSolanaBlockHeight(
        chain_id,
        base::BindOnce(&SolanaTxManager::OnGetBlockHeightForBlockhashHardware,
                       weak_ptr_factory_.GetWeakPtr(), std::move(meta),
                       std::move(callback), blockhash));
  }
}

void SolanaTxManager::OnGetBlockHeightForBlockhashHardware(
    std::unique_ptr<SolanaTxMeta> meta,
    GetTransactionMessageToSignCallback callback,
    const std::string& blockhash,
    uint64_t block_height,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run(nullptr);
    return;
  }

  OnGetLatestBlockhashHardware(std::move(meta), std::move(callback), blockhash,
                               block_height + kValidBlockHeightThreshold,
                               mojom::SolanaProviderError::kSuccess, "");
}

mojom::CoinType SolanaTxManager::GetCoinType() const {
  return mojom::CoinType::SOL;
}

void SolanaTxManager::MakeSystemProgramTransferTxData(
    const std::string& from,
    const std::string& to,
    uint64_t lamports,
    MakeSystemProgramTransferTxDataCallback callback) {
  if (BlockchainRegistry::GetInstance()->IsOfacAddress(to)) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInvalidParams,
        l10n_util::GetStringUTF8(IDS_WALLET_OFAC_RESTRICTION));
    return;
  }

  std::optional<SolanaInstruction> instruction =
      solana::system_program::Transfer(from, to, lamports);
  if (!instruction) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  std::vector<SolanaInstruction> vec;
  vec.emplace_back(std::move(instruction.value()));
  // recent_blockhash will be updated when we are going to send out the tx.
  auto msg = SolanaMessage::CreateLegacyMessage("" /* recent_blockhash*/, 0,
                                                from, std::move(vec));
  if (!msg) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  SolanaTransaction transaction(std::move(*msg));
  transaction.set_to_wallet_address(to);
  transaction.set_tx_type(mojom::TransactionType::SolanaSystemTransfer);
  transaction.set_lamports(lamports);

  auto tx_data = transaction.ToSolanaTxData();
  // This won't be null because we will always construct the mojo struct.
  DCHECK(tx_data);
  std::move(callback).Run(std::move(tx_data),
                          mojom::SolanaProviderError::kSuccess, "");
}

void SolanaTxManager::MakeTokenProgramTransferTxData(
    const std::string& chain_id,
    const std::string& spl_token_mint_address,
    const std::string& from_wallet_address,
    const std::string& to_wallet_address,
    uint64_t amount,
    MakeTokenProgramTransferTxDataCallback callback) {
  if (BlockchainRegistry::GetInstance()->IsOfacAddress(to_wallet_address)) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInvalidParams,
        l10n_util::GetStringUTF8(IDS_WALLET_OFAC_RESTRICTION));
    return;
  }

  std::optional<std::string> from_associated_token_account =
      SolanaKeyring::GetAssociatedTokenAccount(spl_token_mint_address,
                                               from_wallet_address);
  std::optional<std::string> to_associated_token_account =
      SolanaKeyring::GetAssociatedTokenAccount(spl_token_mint_address,
                                               to_wallet_address);
  if (!from_associated_token_account || !to_associated_token_account) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  // Check if the receiver's associated token account is existed or not.
  json_rpc_service_->GetSolanaAccountInfo(
      chain_id, *to_associated_token_account,
      base::BindOnce(
          &SolanaTxManager::OnGetAccountInfo, weak_ptr_factory_.GetWeakPtr(),
          spl_token_mint_address, from_wallet_address, to_wallet_address,
          *from_associated_token_account, *to_associated_token_account, amount,
          std::move(callback)));
}

void SolanaTxManager::MakeTxDataFromBase64EncodedTransaction(
    const std::string& encoded_transaction,
    const mojom::TransactionType tx_type,
    mojom::SolanaSendTransactionOptionsPtr send_options,
    MakeTxDataFromBase64EncodedTransactionCallback callback) {
  std::optional<std::vector<std::uint8_t>> transaction_bytes =
      base::Base64Decode(encoded_transaction);
  if (!transaction_bytes || transaction_bytes->empty() ||
      transaction_bytes->size() > kSolanaMaxTxSize) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  auto transaction =
      SolanaTransaction::FromSignedTransactionBytes(*transaction_bytes);
  if (!transaction) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  transaction->set_tx_type(std::move(tx_type));

  if (send_options) {
    const auto& options = SolanaTransaction::SendOptions::FromMojomSendOptions(
        std::move(send_options));
    transaction->set_send_options(std::move(options));
  }

  auto tx_data = transaction->ToSolanaTxData();
  // This won't be null because we will always construct the mojo struct.
  DCHECK(tx_data);
  std::move(callback).Run(std::move(tx_data),
                          mojom::SolanaProviderError::kSuccess, "");
}

void SolanaTxManager::OnGetAccountInfo(
    const std::string& spl_token_mint_address,
    const std::string& from_wallet_address,
    const std::string& to_wallet_address,
    const std::string& from_associated_token_account,
    const std::string& to_associated_token_account,
    uint64_t amount,
    MakeTokenProgramTransferTxDataCallback callback,
    std::optional<SolanaAccountInfo> account_info,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run(nullptr, error, error_message);
    return;
  }

  bool create_associated_token_account = false;
  std::vector<SolanaInstruction> instructions;
  if (!account_info || account_info->owner != mojom::kSolanaTokenProgramId) {
    std::optional<SolanaInstruction> create_associated_token_instruction =
        solana::spl_associated_token_account_program::
            CreateAssociatedTokenAccount(from_wallet_address, to_wallet_address,
                                         to_associated_token_account,
                                         spl_token_mint_address);
    if (!create_associated_token_instruction) {
      std::move(callback).Run(
          nullptr, mojom::SolanaProviderError::kInternalError,
          l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
      return;
    }
    instructions.push_back(std::move(*create_associated_token_instruction));
    create_associated_token_account = true;
  }

  std::optional<SolanaInstruction> transfer_instruction =
      solana::spl_token_program::Transfer(
          mojom::kSolanaTokenProgramId, from_associated_token_account,
          to_associated_token_account, from_wallet_address,
          std::vector<std::string>(), amount);
  if (!transfer_instruction) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }
  instructions.push_back(std::move(*transfer_instruction));

  // recent_blockhash will be updated when we are going to send out the tx.
  auto msg = SolanaMessage::CreateLegacyMessage("" /* recent_blockhash*/, 0,
                                                from_wallet_address,
                                                std::move(instructions));
  if (!msg) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  SolanaTransaction transaction(std::move(*msg));
  transaction.set_to_wallet_address(to_wallet_address);
  transaction.set_spl_token_mint_address(spl_token_mint_address);
  transaction.set_amount(amount);
  transaction.set_tx_type(
      create_associated_token_account
          ? mojom::TransactionType::
                SolanaSPLTokenTransferWithAssociatedTokenAccountCreation
          : mojom::TransactionType::SolanaSPLTokenTransfer);

  auto tx_data = transaction.ToSolanaTxData();
  // This won't be null because we will always construct the mojo struct.
  DCHECK(tx_data);
  std::move(callback).Run(std::move(tx_data),
                          mojom::SolanaProviderError::kSuccess, "");
}

void SolanaTxManager::GetEstimatedTxFee(const std::string& tx_meta_id,
                                        GetEstimatedTxFeeCallback callback) {
  std::unique_ptr<SolanaTxMeta> meta =
      GetSolanaTxStateManager()->GetSolanaTx(tx_meta_id);
  if (!meta) {
    DCHECK(false) << "Transaction should be found";
    std::move(callback).Run(
        false, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_BRAVE_WALLET_TRANSACTION_NOT_FOUND));
    return;
  }

  auto internal_callback =
      base::BindOnce(&SolanaTxManager::FinishGetEstimatedTxFee,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback));

  GetEstimatedTxFeeAndMeta(std::move(meta), std::move(internal_callback));
}

void SolanaTxManager::GetEstimatedTxFeeAndMeta(
    std::unique_ptr<SolanaTxMeta> meta,
    GetEstimatedTxFeeAndMetaCallback callback) {
  LOG(INFO) << "SolanaTxManager::GetEstimatedTxFeeAndMeta 0";
  auto chain_id = meta->chain_id();
  const std::string blockhash = meta->tx()->message()->recent_blockhash();
  if (blockhash.empty()) {
    GetSolanaBlockTracker()->GetLatestBlockhash(
        chain_id,
        base::BindOnce(
            &SolanaTxManager::OnGetLatestBlockhashForGetEstimatedTxFee,
            weak_ptr_factory_.GetWeakPtr(), std::move(meta),
            std::move(callback)),
        true);
  } else {
    const std::string base64_encoded_message =
        meta->tx()->GetBase64EncodedMessage();
    json_rpc_service_->GetSolanaFeeForMessage(
        meta->chain_id(), base64_encoded_message,
        base::BindOnce(&SolanaTxManager::OnGetFeeForMessage,
                       weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                       std::move(meta)));
  }
}

void SolanaTxManager::OnGetLatestBlockhashForGetEstimatedTxFee(
    std::unique_ptr<SolanaTxMeta> meta,
    GetEstimatedTxFeeAndMetaCallback callback,
    const std::string& latest_blockhash,
    uint64_t last_valid_block_height,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  LOG(INFO) << "SolanaTxManager::OnGetLatestBlockhashForGetEstimatedTxFee 0";
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run({}, 0, error, error_message);
    return;
  }

  meta->tx()->message()->set_recent_blockhash(latest_blockhash);
  meta->tx()->message()->set_last_valid_block_height(last_valid_block_height);
  const std::string base64_encoded_message =
      meta->tx()->GetBase64EncodedMessage();
  json_rpc_service_->GetSolanaFeeForMessage(
      meta->chain_id(), base64_encoded_message,
      base::BindOnce(&SolanaTxManager::OnGetFeeForMessage,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     std::move(meta)));
}

void SolanaTxManager::OnGetFeeForMessage(
    GetEstimatedTxFeeAndMetaCallback callback,
    std::unique_ptr<SolanaTxMeta> meta,
    uint64_t tx_fee,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  LOG(ERROR) << "SolanaTxManager::OnGetFeeForMessage 0";
  std::move(callback).Run(std::move(meta), tx_fee, error, error_message);
}

void SolanaTxManager::GetSolanaGasEstimation(
    const std::string& chain_id,
    const std::string& tx_meta_id,
    GetSolanaGasEstimationCallback callback) {
  // Get the TxMeta.
  std::unique_ptr<SolanaTxMeta> meta =
      GetSolanaTxStateManager()->GetSolanaTx(tx_meta_id);
  if (!meta) {
    std::move(callback).Run(
        {}, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_BRAVE_WALLET_TRANSACTION_NOT_FOUND));
    return;
  }

  auto internal_callback =
      base::BindOnce(&SolanaTxManager::FinishGetSolanaGasEstimation,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback));

  GetSolanaGasEstimationAndMeta(chain_id, std::move(meta),
                                std::move(internal_callback));
}

void SolanaTxManager::GetSolanaGasEstimationAndMeta(
    const std::string& chain_id,
    std::unique_ptr<SolanaTxMeta> meta,
    GetSolanaGasEstimationAndMetaCallback callback) {
  auto internal_callback = base::BindOnce(
      &SolanaTxManager::OnGetEstimatedTxFeeAndMeta,
      weak_ptr_factory_.GetWeakPtr(), chain_id, std::move(callback));
  GetEstimatedTxFeeAndMeta(std::move(meta), std::move(internal_callback));
}

void SolanaTxManager::OnLatestBlockhashUpdated(
    const std::string& chain_id,
    const std::string& blockhash,
    uint64_t last_valid_block_height) {
  UpdatePendingTransactions(chain_id);
}

SolanaTxStateManager* SolanaTxManager::GetSolanaTxStateManager() {
  return static_cast<SolanaTxStateManager*>(tx_state_manager_.get());
}

SolanaBlockTracker* SolanaTxManager::GetSolanaBlockTracker() {
  return static_cast<SolanaBlockTracker*>(block_tracker_.get());
}

std::unique_ptr<SolanaTxMeta> SolanaTxManager::GetTxForTesting(
    const std::string& tx_meta_id) {
  return GetSolanaTxStateManager()->GetSolanaTx(tx_meta_id);
}

void SolanaTxManager::ProcessSolanaHardwareSignature(
    const std::string& tx_meta_id,
    const std::vector<uint8_t>& signature_bytes,
    ProcessSolanaHardwareSignatureCallback callback) {
  std::unique_ptr<SolanaTxMeta> meta =
      GetSolanaTxStateManager()->GetSolanaTx(tx_meta_id);
  if (!meta) {
    std::move(callback).Run(
        false,
        mojom::ProviderErrorUnion::NewSolanaProviderError(
            mojom::SolanaProviderError::kInternalError),
        l10n_util::GetStringUTF8(IDS_BRAVE_WALLET_TRANSACTION_NOT_FOUND));
    return;
  }
  std::optional<std::vector<std::uint8_t>> transaction_bytes =
      meta->tx()->GetSignedTransactionBytes(keyring_service_, meta->from(),
                                            &signature_bytes);
  if (!transaction_bytes) {
    std::move(callback).Run(
        false,
        mojom::ProviderErrorUnion::NewSolanaProviderError(
            mojom::SolanaProviderError::kInternalError),
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  meta->set_status(mojom::TransactionStatus::Approved);
  if (!tx_state_manager_->AddOrUpdateTx(*meta)) {
    std::move(callback).Run(
        false,
        mojom::ProviderErrorUnion::NewSolanaProviderError(
            mojom::SolanaProviderError::kInternalError),
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  json_rpc_service_->SendSolanaTransaction(
      meta->chain_id(), base::Base64Encode(*transaction_bytes),
      meta->tx()->send_options(),
      base::BindOnce(&SolanaTxManager::OnSendSolanaTransaction,
                     weak_ptr_factory_.GetWeakPtr(), meta->id(),
                     std::move(callback)));
}

}  // namespace brave_wallet
