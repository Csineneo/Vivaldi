// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_CHROMEOS_NETWORK_NETWORK_LIST_MD_H_
#define UI_CHROMEOS_NETWORK_NETWORK_LIST_MD_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/macros.h"
#include "chromeos/network/network_state_handler.h"
#include "ui/chromeos/network/network_icon_animation_observer.h"
#include "ui/chromeos/network/network_list_view_base.h"
#include "ui/chromeos/ui_chromeos_export.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/controls/button/button.h"

namespace views {
class Label;
class View;
}

namespace ui {

struct NetworkInfo;
class NetworkListDelegate;

// A list of available networks of a given type. This class is used for all
// network types except VPNs. For VPNs, see the |VPNList| class.
class UI_CHROMEOS_EXPORT NetworkListViewMd
    : public NetworkListViewBase,
      public network_icon::AnimationObserver,
      public views::ButtonListener {
 public:
  explicit NetworkListViewMd(NetworkListDelegate* delegate);
  ~NetworkListViewMd() override;

  // NetworkListViewBase:
  void Update() override;
  bool IsNetworkEntry(views::View* view,
                      std::string* service_path) const override;

 private:
  class WifiHeaderRowView;

  // Clears |network_list_| and adds to it |networks| that match |delegate_|'s
  // network type pattern.
  void UpdateNetworks(
      const chromeos::NetworkStateHandler::NetworkStateList& networks);

  // Updates |network_list_| entries and sets |this| to observe network icon
  // animations when any of the networks are in connecting state.
  void UpdateNetworkIcons();

  // Orders entries in |network_list_| such that higher priority network types
  // are at the top of the list.
  void OrderNetworks();

  // Refreshes a list of child views, updates |network_map_| and
  // |service_path_map_| and performs layout making sure selected view if any is
  // scrolled into view.
  void UpdateNetworkListInternal();

  // Adds new or updates existing child views including header row and messages.
  // Returns a set of service paths for the added network connections.
  std::unique_ptr<std::set<std::string>> UpdateNetworkListEntries();

  // Adds or updates child views representing the network connections when
  // |is_wifi| is matching the attribute of a network connection starting at
  // |child_index|. Returns a set of service paths for the added network
  // connections.
  std::unique_ptr<std::set<std::string>> UpdateNetworkChildren(bool is_wifi,
                                                               int child_index);
  void UpdateNetworkChild(int index, const NetworkInfo* info);

  // Reorders children of |container()| as necessary placing |view| at |index|.
  void PlaceViewAtIndex(views::View* view, int index);

  // Creates a Label with text specified by |message_id| and adds it to
  // |container()| if necessary or updates the text and reorders the
  // |container()| placing the label at |insertion_index|. When |message_id| is
  // zero removes the |*label_ptr| from the |container()| and destroys it.
  // |label_ptr| is an in / out parameter and is only modified if the Label is
  // created or destroyed.
  void UpdateInfoLabel(int message_id,
                       int insertion_index,
                       views::Label** label_ptr);

  // Creates a Wi-Fi header row |view| and adds it to |container()| if necessary
  // and reorders the |container()| placing the |view| at |child_index|.
  void UpdateWifiHeaderRow(bool enabled,
                           int child_index,
                           WifiHeaderRowView** view);

  // network_icon::AnimationObserver:
  void NetworkIconChanged() override;

  // views::ButtonListener:
  void ButtonPressed(views::Button* sender, const ui::Event& event) override;

  bool needs_relayout_;
  NetworkListDelegate* delegate_;

  views::Label* no_wifi_networks_view_;
  views::Label* no_cellular_networks_view_;
  WifiHeaderRowView* wifi_header_view_;

  // An owned list of network info.
  std::vector<std::unique_ptr<NetworkInfo>> network_list_;

  using NetworkMap = std::map<views::View*, std::string>;
  NetworkMap network_map_;

  // A map of network service paths to their view.
  typedef std::map<std::string, views::View*> ServicePathMap;
  ServicePathMap service_path_map_;

  DISALLOW_COPY_AND_ASSIGN(NetworkListViewMd);
};

}  // namespace ui

#endif  // UI_CHROMEOS_NETWORK_NETWORK_LIST_MD_H_
