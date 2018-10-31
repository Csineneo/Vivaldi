// Copyright (c) 2018 Vivaldi Technologies AS. All rights reserved
//
// Based on code that is:
//
// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/blink/vivaldi_add_search_context_menu.h"

#include "third_party/blink/renderer/bindings/core/v8/v8_html_form_element.h"
#include "third_party/blink/renderer/core/dom/element_traversal.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/dom/node_traversal.h"
#include "third_party/blink/renderer/core/editing/frame_selection.h"
#include "third_party/blink/renderer/core/editing/visible_selection.h"
#include "third_party/blink/renderer/core/html/html_element.h"
#include "third_party/blink/renderer/core/html/html_frame_element_base.h"
#include "third_party/blink/renderer/core/html/forms/html_form_control_element.h"
#include "third_party/blink/public/platform/web_url.h"
#include "third_party/blink/public/web/web_searchable_form_data.h"
#include "third_party/blink/public/web/web_form_element.h"

using blink::FrameSelection;
using blink::HTMLElement;
using blink::HTMLFormElement;
using blink::Node;
using blink::NodeTraversal;
using blink::Traversal;
using blink::WebFormElement;
using blink::WebSearchableFormData;
using blink::WebURL;

namespace vivaldi {

namespace {

HTMLFormElement* AssociatedFormElement(HTMLElement& element) {
  if (auto* form = ToHTMLFormElementOrNull(element))
    return form;
  return element.formOwner();
}

// Scans logically forward from "start", including any child frames.
HTMLFormElement* ScanForForm(const Node* start) {
  if (!start)
    return nullptr;

  for (HTMLElement& element : Traversal<HTMLElement>::StartsAt(
           start->IsHTMLElement() ? ToHTMLElement(start)
                                  : Traversal<HTMLElement>::Next(*start))) {
    if (HTMLFormElement* form = AssociatedFormElement(element))
      return form;

    if (IsHTMLFrameElementBase(element)) {
      Node* child_document = ToHTMLFrameElementBase(element).contentDocument();
      if (HTMLFormElement* frame_result = ScanForForm(child_document))
        return frame_result;
    }
  }
  return nullptr;
}

// We look for either the form containing the current focus, or for one
// immediately after it
HTMLFormElement* CurrentForm(const FrameSelection& current_selection) {
  // Start looking either at the active (first responder) node, or where the
  // selection is.
  const Node* start = current_selection.GetDocument().FocusedElement();
  if (!start) {
    start = current_selection.ComputeVisibleSelectionInDOMTree()
                .Start()
                .AnchorNode();
  }
  if (!start)
    return nullptr;

  // Try walking up the node tree to find a form element.
  for (Node& node : NodeTraversal::InclusiveAncestorsOf(*start)) {
    if (!node.IsHTMLElement())
      break;
    HTMLElement& element = ToHTMLElement(node);
    if (HTMLFormElement* form = AssociatedFormElement(element))
      return form;
  }

  // Try walking forward in the node tree to find a form element.
  return ScanForForm(start);
}
}  // namespace

WebURL GetWebSearchableUrl(const FrameSelection& currentSelection,
                           HTMLInputElement* selected_element) {
  auto* form = CurrentForm(currentSelection);
  if (form && selected_element) {
    WebSearchableFormData ws =
        WebSearchableFormData(WebFormElement(form), selected_element);

    return ws.Url();
  }
  return WebURL();
}

}  // namespace vivaldi
