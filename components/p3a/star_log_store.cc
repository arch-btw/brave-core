/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/p3a/star_log_store.h"

#include <memory>
#include <set>

#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "brave/components/p3a/uploader.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace p3a {

namespace {

constexpr char kPrefName[] = "p3a.star_logs";

}  // namespace

StarLogStore::StarLogStore(PrefService* local_state, size_t keep_epoch_count)
    : local_state_(local_state), keep_epoch_count_(keep_epoch_count) {
  DCHECK(local_state);
  DCHECK_GT(keep_epoch_count, 0U);
}

StarLogStore::~StarLogStore() = default;

bool StarLogStore::LogKeyCompare::operator()(const LogKey& lhs,
                                             const LogKey& rhs) const {
  return std::tie(lhs.epoch, lhs.histogram_name) <
         std::tie(rhs.epoch, rhs.histogram_name);
}

void StarLogStore::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kPrefName);
}

void StarLogStore::UpdateMessage(const std::string& histogram_name,
                                 uint8_t epoch,
                                 const std::string& msg) {
  ScopedDictPrefUpdate update(local_state_, kPrefName);
  std::string epoch_key = base::NumberToString(epoch);
  base::Value::Dict* epoch_dict = update->EnsureDict(epoch_key);
  epoch_dict->Set(histogram_name, msg);

  if (current_epoch_ != epoch) {
    LogKey key(epoch, histogram_name);
    log_[key] = msg;
    unsent_entries_.insert(key);
  }
}

void StarLogStore::RemoveMessageIfExists(const LogKey& key) {
  log_.erase(key);
  unsent_entries_.erase(key);

  // Update the persistent value.
  ScopedDictPrefUpdate update(local_state_, kPrefName);
  std::string epoch_key = base::NumberToString(key.epoch);
  base::Value::Dict* epoch_dict = update->EnsureDict(epoch_key);
  epoch_dict->Remove(key.histogram_name);

  if (has_staged_log() && staged_entry_key_->epoch == key.epoch &&
      staged_entry_key_->histogram_name == key.histogram_name) {
    staged_entry_key_ = nullptr;
    staged_log_.clear();
  }
}

void StarLogStore::SetCurrentEpoch(uint8_t current_epoch) {
  current_epoch_ = current_epoch;
}

bool StarLogStore::has_unsent_logs() const {
  return !unsent_entries_.empty();
}

bool StarLogStore::has_staged_log() const {
  return staged_entry_key_ != nullptr;
}

const std::string& StarLogStore::staged_log() const {
  DCHECK(staged_entry_key_);
  DCHECK(!staged_log_.empty());

  return staged_log_;
}

std::string StarLogStore::staged_log_type() const {
  DCHECK(staged_entry_key_);
  if (base::StartsWith(staged_entry_key_->histogram_name, "Brave.P2A",
                       base::CompareCase::SENSITIVE)) {
    return kP2AUploadType;
  }
  return kP3AUploadType;
}

const std::string& StarLogStore::staged_log_hash() const {
  NOTREACHED();
  return staged_log_hash_;
}

const std::string& StarLogStore::staged_log_signature() const {
  NOTREACHED();
  return staged_log_signature_;
}

absl::optional<uint64_t> StarLogStore::staged_log_user_id() const {
  NOTREACHED();
  return absl::nullopt;
}

void StarLogStore::StageNextLog() {
  // Stage the next item.
  DCHECK(has_unsent_logs());
  uint64_t rand_idx = base::RandGenerator(unsent_entries_.size());
  staged_entry_key_ =
      std::make_unique<LogKey>(*(unsent_entries_.begin() + rand_idx));

  staged_log_ = log_.at(*staged_entry_key_);

  VLOG(2) << "StarLogStore::StageNextLog: staged epoch = "
          << static_cast<int>(staged_entry_key_->epoch)
          << ", histogram_name = " << staged_entry_key_->histogram_name;
}

void StarLogStore::DiscardStagedLog() {
  if (!has_staged_log()) {
    return;
  }
  staged_entry_key_ = nullptr;
  staged_log_.clear();
}

void StarLogStore::MarkStagedLogAsSent() {
  if (!has_staged_log()) {
    return;
  }
  RemoveMessageIfExists(*staged_entry_key_);
}

void StarLogStore::TrimAndPersistUnsentLogs(bool overwrite_in_memory_store) {
  NOTREACHED();
}

void StarLogStore::LoadPersistedUnsentLogs() {
  log_.clear();
  unsent_entries_.clear();

  std::vector<std::string> epochs_to_remove;

  const base::Value::Dict& log_dict = local_state_->GetDict(kPrefName);
  for (const auto [epoch_key, inner_epoch_dict] : log_dict) {
    uint64_t parsed_epoch;
    if (!base::StringToUint64(epoch_key, &parsed_epoch)) {
      continue;
    }
    uint8_t item_epoch = (uint8_t)parsed_epoch;

    if (current_epoch_ == item_epoch) {
      // Do not load/send messages from the current epoch
      continue;
    }

    if ((current_epoch_ - item_epoch) >= keep_epoch_count_) {
      // If epoch is too old, delete it
      epochs_to_remove.push_back(epoch_key);
      continue;
    }

    for (const auto [histogram_name, log_value] :
         inner_epoch_dict.DictItems()) {
      if (!log_value.is_string()) {
        continue;
      }
      LogKey key(item_epoch, histogram_name);
      log_[key] = log_value.GetString();

      unsent_entries_.insert(key);
    }
  }

  if (!epochs_to_remove.empty()) {
    ScopedDictPrefUpdate update(local_state_, kPrefName);
    for (const std::string& epoch : epochs_to_remove) {
      update->Remove(epoch);
    }
  }
}

}  // namespace p3a
