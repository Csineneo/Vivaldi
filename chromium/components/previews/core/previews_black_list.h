// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PREVIEWS_CORE_PREVIEWS_BLACK_LIST_H_
#define COMPONENTS_PREVIEWS_CORE_PREVIEWS_BLACK_LIST_H_

#include <stdint.h>

#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "components/previews/core/previews_opt_out_store.h"

class GURL;

namespace base {
class Clock;
}

namespace previews {
class PreviewsBlackListItem;

// Manages the state of black listed domains for the previews experiment. Loads
// the stored black list from |opt_out_store| and manages an in memory black
// list on the IO thread. Updates to the black list are stored in memory and
// pushed to the store. Asynchronous modifications are stored in a queue and
// executed in order. Reading from the black list is always synchronous, and if
// the black list is not currently loaded (e.g., at startup, after clearing
// browsing history), domains are reported as black listed. The list stores no
// more than previews::params::MaxInMemoryHostsInBlackList hosts in-memory,
// which defaults to 100.
class PreviewsBlackList {
 public:
  // |opt_out_store| is the backing store to retrieve and store black list
  // information, and can be null. When |opt_out_store| is null, the in-memory
  // map will be immediately loaded to empty. If |opt_out_store| is non-null,
  // it will be used to load the in-memory map asynchronously.
  PreviewsBlackList(std::unique_ptr<PreviewsOptOutStore> opt_out_store,
                    std::unique_ptr<base::Clock> clock);
  ~PreviewsBlackList();

  // Asynchronously adds a new navigation to to the in-memory black list and
  // backing store. |opt_out| is whether the user opted out of the preview or
  // navigated away from the page without opting out. |type| is only passed to
  // the backing store. If the in memory map has reached the max number of hosts
  // allowed, and |url| is a new host, a host will be evicted based on recency
  // of the hosts most recent opt out.
  void AddPreviewNavigation(const GURL& url, bool opt_out, PreviewsType type);

  // Synchronously determines if |host_name| should be allowed to show previews.
  // If the black list has loaded yet, this will always return false. |type| is
  // not used to make this decision.
  bool IsLoadedAndAllowed(const GURL& url, PreviewsType type) const;

  // Asynchronously deletes all entries in the in-memory black list. Informs
  // the backing store to delete entries between |begin_time| and |end_time|,
  // and reloads entries into memory from the backing store. If the embedder
  // passed in a null store, resets all history in the in-memory black list.
  void ClearBlackList(base::Time begin_time, base::Time end_time);

 private:
  // Synchronous version of AddPreviewNavigation method.
  void AddPreviewNavigationSync(const GURL& host_name,
                                bool opt_out,
                                PreviewsType type);

  // Returns the PreviewsBlackListItem representing |host_name|. If there is no
  // item for |host_name|, returns null.
  PreviewsBlackListItem* GetBlackListItem(const std::string& host_name) const;

  // Synchronous version of ClearBlackList method.
  void ClearBlackListSync(base::Time begin_time, base::Time end_time);

  // Returns a new PreviewsBlackListItem representing |host_name|. Adds the new
  // item to the in-memory map.
  PreviewsBlackListItem* CreateBlackListItem(const std::string& host_name);

  // Callback passed to the backing store when loading black list information.
  // Moves the returned map into the in-memory black list and runs any
  // outstanding tasks.
  void LoadBlackListDone(std::unique_ptr<BlackListItemMap> black_list_item_map);

  // Called while waiting for the black list to be loaded from the backing
  // store.
  // Enqueues a task to run when when loading black list information has
  // completed. Maintains the order that tasks were called in.
  void QueuePendingTask(base::Closure callback);

  // Evicts one entry from the in-memory black list based on recency of a hosts
  // most recent opt out time.
  void EvictOldestOptOut();

  // Map maintaining the in-memory black list.
  std::unique_ptr<BlackListItemMap> black_list_item_map_;

  // Whether the black list is done being loaded from the backing store.
  bool loaded_;

  // The backing store of the black list information.
  std::unique_ptr<PreviewsOptOutStore> opt_out_store_;

  // Callbacks to be run after loading information from the backing store has
  // completed.
  std::queue<base::Closure> pending_callbacks_;

  std::unique_ptr<base::Clock> clock_;

  base::ThreadChecker thread_checker_;

  base::WeakPtrFactory<PreviewsBlackList> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(PreviewsBlackList);
};

}  // namespace previews

#endif  // COMPONENTS_PREVIEWS_CORE_PREVIEWS_BLACK_LIST_H_
