// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_OFFLINE_ITEMS_COLLECTION_CORE_OFFLINE_ITEM_H_
#define COMPONENTS_OFFLINE_ITEMS_COLLECTION_CORE_OFFLINE_ITEM_H_

#include <string>

#include "base/time/time.h"
#include "components/offline_items_collection/core/offline_item_filter.h"
#include "components/offline_items_collection/core/offline_item_state.h"
#include "url/gurl.h"

namespace offline_items_collection {

// An id that uniquely represents a piece of offline content.
struct ContentId {
  // The namespace for the offline content.  This will be used to associate this
  // id with a particular OfflineContentProvider.  A name_space can include
  // any characters except ','.  This is due to a serialization format
  // limitation.
  // TODO(dtrainor): Remove the 'no ,' limitation.
  std::string name_space;

  // The id of the offline item.
  std::string id;

  ContentId();
  ContentId(const ContentId& other);
  ContentId(const std::string& name_space, const std::string& id);

  ~ContentId();

  bool operator==(const ContentId& content_id) const;

  bool operator<(const ContentId& content_id) const;
};

// This struct holds the relevant pieces of information to represent an abstract
// offline item to the front end.  This is meant to be backed by components that
// need to both show content being offlined (downloading, saving, etc.) as well
// as content that should be exposed as available offline (downloads, pages,
// etc.).
//
// A new feature should expose these OfflineItems via an OfflineContentProvider.
struct OfflineItem {
  OfflineItem();
  OfflineItem(const OfflineItem& other);
  explicit OfflineItem(const ContentId& id);

  ~OfflineItem();

  bool operator==(const OfflineItem& offline_item) const;

  // The id of this OfflineItem.  Used to identify this item across all relevant
  // systems.
  ContentId id;

  // Display Metadata.
  // ---------------------------------------------------------------------------
  // The title of the OfflineItem to display in the UI.
  std::string title;

  // The description of the OfflineItem to display in the UI (may or may not be
  // displayed depending on the specific UI component).
  std::string description;

  // The type of offline item this is.  This can be used for filtering offline
  // items as well as for determining which default icon to use.
  OfflineItemFilter filter;

  // Whether or not this item is transient.  Transient items won't show up in
  // persistent UI spaces and will only show up as notifications.
  bool is_transient;

  // TODO(dtrainor): Build out custom per-item icon support.

  // Content Metadata.
  // ---------------------------------------------------------------------------
  // The total size of the offline item as best known at the current time.
  int64_t total_size_bytes;

  // Whether or not this item has been removed externally (not by Chrome).
  bool externally_removed;

  // The time when the underlying offline content was created.
  base::Time creation_time;

  // The last time the underlying offline content was accessed.
  base::Time last_accessed_time;

  // Whether or not this item can be opened after it is done being downloaded.
  bool is_openable;

  // Request Metadata.
  // ---------------------------------------------------------------------------
  // The URL of the top level frame at the time the content was offlined.
  GURL page_url;

  // The URL that represents the original request (before any redirection).
  GURL original_url;

  // Whether or not this item is off the record.
  bool is_off_the_record;

  // In Progress Metadata.
  // ---------------------------------------------------------------------------
  // The current state of the OfflineItem.
  OfflineItemState state;

  // Whether or not the offlining of this content can be resumed if it was
  // paused or interrupted.
  bool is_resumable;

  // Whether or not this OfflineItem can be downloaded using a metered
  // connection.
  bool allow_metered;

  // The current amount of bytes received for this item.  This field is not used
  // if |state| is COMPLETE.
  int64_t received_bytes;

  // How complete (from 0 to 100) the offlining process is for this item.  -1
  // represents that progress cannot be determined for this item and an
  // indeterminate progress bar should be used.  This field is not used if
  // |state| is COMPLETE.
  int percent_completed;

  // The estimated time remaining for the download in milliseconds.  -1
  // represents an unknown time remaining.  This field is not used if |state| is
  // COMPLETE.
  int64_t time_remaining_ms;
};

}  // namespace offline_items_collection

#endif  // COMPONENTS_OFFLINE_ITEMS_COLLECTION_OFFLINE_ITEM_H_
