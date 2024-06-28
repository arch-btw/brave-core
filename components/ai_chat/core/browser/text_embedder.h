/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_TEXT_EMBEDDER_H_
#define BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_TEXT_EMBEDDER_H_

#include <memory>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/types/expected.h"
#include "third_party/tflite_support/src/tensorflow_lite_support/cc/task/text/text_embedder.h"

namespace base {
class SequencedTaskRunner;
}  // namespace base

namespace ai_chat {
class TextEmbedder {
 public:
  static std::unique_ptr<TextEmbedder> Create(const base::FilePath& model_path);

  virtual ~TextEmbedder();

  using TopSimilarityCallback =
      base::OnceCallback<void(base::expected<std::string, std::string>)>;
  void GetTopSimilarityWithPromptTilContextLimit(
      const std::string& prompt,
      const std::string& text,
      uint32_t context_limit,
      TopSimilarityCallback callback);

 private:
  TextEmbedder();

  void GetTopSimilarityWithPromptTilContextLimitInternal(
      const std::string& prompt,
      const std::string& text,
      uint32_t context_limit,
      TopSimilarityCallback callback);

  std::vector<std::string> SplitSegments(const std::string& text);
  // std::vector<std::string> SplitSCSegments(const std::string& text);

  absl::Status EmbedText(const std::string& text,
                         tflite::task::processor::EmbeddingResult& embedding);

  absl::Status EmbedSegments();

  size_t text_hash_ = 0;
  std::vector<std::string> segments_;
  std::vector<tflite::task::processor::EmbeddingResult> embeddings_;
  std::unique_ptr<tflite::task::text::TextEmbedder> tflite_text_embedder_;

  scoped_refptr<base::SequencedTaskRunner> owner_task_runner_;
  SEQUENCE_CHECKER(owner_sequence_checker_);

  scoped_refptr<base::SequencedTaskRunner> embedder_task_runner_;
  SEQUENCE_CHECKER(embedder_sequence_checker_);

  base::WeakPtrFactory<TextEmbedder> weak_ptr_factory_{this};
};
}  // namespace ai_chat
#endif  // BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_TEXT_EMBEDDER_H_
