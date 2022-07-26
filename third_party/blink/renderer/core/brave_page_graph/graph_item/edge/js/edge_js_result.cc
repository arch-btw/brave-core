/* Copyright (c) 2019 The Brave Software Team. Distributed under the MPL2
 * license. This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/third_party/blink/renderer/core/brave_page_graph/graph_item/edge/js/edge_js_result.h"

#include <libxml/tree.h>
#include <string>

#include "third_party/blink/renderer/platform/wtf/casting.h"

#include "brave/third_party/blink/renderer/core/brave_page_graph/graph_item/node/actor/node_script.h"
#include "brave/third_party/blink/renderer/core/brave_page_graph/graph_item/node/js/node_js.h"
#include "brave/third_party/blink/renderer/core/brave_page_graph/graphml.h"
#include "brave/third_party/blink/renderer/core/brave_page_graph/logging.h"
#include "brave/third_party/blink/renderer/core/brave_page_graph/page_graph.h"
#include "brave/third_party/blink/renderer/core/brave_page_graph/types.h"

using ::blink::To;

namespace brave_page_graph {

EdgeJSResult::EdgeJSResult(PageGraph* const graph,
                           NodeJS* const out_node,
                           NodeScript* const in_node,
                           const std::string& result)
    : EdgeJS(graph, out_node, in_node), result_(result) {}

EdgeJSResult::~EdgeJSResult() {}

ItemName EdgeJSResult::GetItemName() const {
  return "js result";
}

ItemDesc EdgeJSResult::GetItemDesc() const {
  return GetItemName() + " [result: " + result_ + "]";
}

void EdgeJSResult::AddGraphMLAttributes(xmlDocPtr doc,
                                        xmlNodePtr parent_node) const {
  EdgeJS::AddGraphMLAttributes(doc, parent_node);
  GraphMLAttrDefForType(kGraphMLAttrDefValue)
      ->AddValueNode(doc, parent_node, result_);
}

const std::string& EdgeJSResult::GetResult() const {
  return result_;
}

const MethodName& EdgeJSResult::GetMethodName() const {
  PG_LOG_ASSERT(GetOutNode()->IsNodeJS());
  return To<NodeJS>(GetOutNode())->GetMethodName();
}

bool EdgeJSResult::IsEdgeJSResult() const {
  return true;
}

}  // namespace brave_page_graph
