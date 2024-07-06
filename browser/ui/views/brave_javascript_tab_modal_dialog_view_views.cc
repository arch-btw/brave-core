/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/brave_javascript_tab_modal_dialog_view_views.h"

#include <utility>

#include "base/functional/bind.h"
#include "brave/browser/ui/tabs/split_view_browser_data.h"
#include "brave/browser/ui/views/frame/brave_browser_view.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/javascript_dialogs/javascript_tab_modal_dialog_manager_delegate_desktop.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "ui/base/metadata/metadata_impl_macros.h"

BraveJavaScriptTabModalDialogViewViews::BraveJavaScriptTabModalDialogViewViews(
    content::WebContents* parent_web_contents,
    content::WebContents* alerting_web_contents,
    const std::u16string& title,
    content::JavaScriptDialogType dialog_type,
    const std::u16string& message_text,
    const std::u16string& default_prompt_text,
    content::JavaScriptDialogManager::DialogClosedCallback dialog_callback,
    base::OnceClosure dialog_force_closed_callback)
    : JavaScriptTabModalDialogViewViews(
          parent_web_contents,
          alerting_web_contents,
          title,
          dialog_type,
          message_text,
          default_prompt_text,
          std::move(dialog_callback),
          std::move(dialog_force_closed_callback)),
      alerting_web_contents_(alerting_web_contents) {
  // JavaScriptTabModalDialogViewViews already created Widget.
  auto* widget = GetWidget();
  CHECK(widget);

  widget->widget_delegate()->set_desired_position_delegate(base::BindRepeating(
      [](base::WeakPtr<BraveJavaScriptTabModalDialogViewViews> dialog_view) {
        if (!dialog_view) {
          return gfx::Point();
        }
        return dialog_view->GetDesiredPositionConsideringSplitView();
      },
      weak_ptr_factory_.GetWeakPtr()));

  UpdateWidgetBounds();
}

BraveJavaScriptTabModalDialogViewViews::
    ~BraveJavaScriptTabModalDialogViewViews() = default;

base::WeakPtr<javascript_dialogs::TabModalDialogView>
JavaScriptTabModalDialogManagerDelegateDesktop::CreateNewDialog(
    content::WebContents* alerting_web_contents,
    const std::u16string& title,
    content::JavaScriptDialogType dialog_type,
    const std::u16string& message_text,
    const std::u16string& default_prompt_text,
    content::JavaScriptDialogManager::DialogClosedCallback dialog_callback,
    base::OnceClosure dialog_force_closed_callback) {
  auto* browser = chrome::FindBrowserWithTab(alerting_web_contents);
  if (!browser) {
    // Can be popup up or other type of window.
    return CreateNewDialog_ChromiumImpl(
        alerting_web_contents, title, dialog_type, message_text,
        default_prompt_text, std::move(dialog_callback),
        std::move(dialog_force_closed_callback));
  }

  if (!SplitViewBrowserData::FromBrowser(browser)) {
    // Split view isn't enabled.
    return CreateNewDialog_ChromiumImpl(
        alerting_web_contents, title, dialog_type, message_text,
        default_prompt_text, std::move(dialog_callback),
        std::move(dialog_force_closed_callback));
  }

  return (new BraveJavaScriptTabModalDialogViewViews(
              web_contents_, alerting_web_contents, title, dialog_type,
              message_text, default_prompt_text, std::move(dialog_callback),
              std::move(dialog_force_closed_callback)))
      ->weak_ptr_factory_.GetWeakPtr();
}

web_modal::WebContentsModalDialogHost*
BraveJavaScriptTabModalDialogViewViews::GetModalDialogHost() {
  web_modal::WebContentsModalDialogManager* manager =
      web_modal::WebContentsModalDialogManager::FromWebContents(
          alerting_web_contents_);
  CHECK(manager);

  auto* modal_dialog_host =
      manager->delegate()->GetWebContentsModalDialogHost();
  CHECK(modal_dialog_host);

  return modal_dialog_host;
}

void BraveJavaScriptTabModalDialogViewViews::UpdateWidgetBounds() {
  auto* widget = GetWidget();
  if (!widget) {
    return;
  }

  CHECK(has_desired_bounds_delegate());
  widget->SetBounds(GetDesiredWidgetBounds());
}

gfx::Point BraveJavaScriptTabModalDialogViewViews::
    GetDesiredPositionConsideringSplitView() {
  auto* widget = GetWidget();
  CHECK(widget);

  auto* modal_dialog_host = GetModalDialogHost();

  // When a tab is in split view mode, We want javascript dialog to be
  // centered to the relevant web view.
  gfx::Rect bounds = widget->GetWindowBoundsInScreen();
  bounds.set_origin(modal_dialog_host->GetDialogPosition(bounds.size()));

  // 1. Check if the tab is in split view mode.
  auto* browser = chrome::FindBrowserWithTab(alerting_web_contents_);
  CHECK(browser);

  auto* tab_strip_model = browser->tab_strip_model();
  auto tab_handle = tab_strip_model->GetTabHandleAt(
      tab_strip_model->GetIndexOfWebContents(alerting_web_contents_));

  auto* split_view_browser_data = SplitViewBrowserData::FromBrowser(browser);
  CHECK(split_view_browser_data);

  auto tile = split_view_browser_data->GetTile(tab_handle);
  if (!tile) {
    return bounds.origin();
  }

  // 2. It's in split view mode. Center the dialog to the relevant web
  // view.
  auto* browser_view = static_cast<BraveBrowserView*>(browser->window());
  auto* target_web_view =
      tab_strip_model->GetActiveTab()->GetHandle() == tab_handle
          ? browser_view->contents_web_view()
          : browser_view->secondary_contents_web_view();
  auto target_web_view_bounds = target_web_view->bounds();

  // Adjust X position
  bounds.set_x(target_web_view_bounds.CenterPoint().x() - bounds.width() / 2);
  target_web_view_bounds =
      views::View::ConvertRectToWidget(target_web_view_bounds);

  return bounds.origin();
}

BEGIN_METADATA(BraveJavaScriptTabModalDialogViewViews)
END_METADATA
