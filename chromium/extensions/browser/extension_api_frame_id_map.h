// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_EXTENSION_API_FRAME_ID_MAP_H_
#define EXTENSIONS_BROWSER_EXTENSION_API_FRAME_ID_MAP_H_

#include <list>
#include <map>

#include "base/callback.h"
#include "base/lazy_instance.h"
#include "base/macros.h"
#include "base/synchronization/lock.h"

namespace content {
class RenderFrameHost;
class WebContents;
}  // namespace content

namespace extensions {

// Extension frame IDs are exposed through the chrome.* APIs and have the
// following characteristics:
// - The top-level frame has ID 0.
// - Any child frame has a positive ID.
// - A non-existant frame has ID -1.
// - They are only guaranteed to be unique within a tab.
// - The ID does not change during the frame's lifetime and is not re-used after
//   the frame is removed. The frame may change its current RenderFrameHost over
//   time, so multiple RenderFrameHosts may map to the same extension frame ID.

// This class provides a mapping from a (render_process_id, frame_routing_id)
// pair that maps a RenderFrameHost to an extension frame ID.
// Unless stated otherwise, the methods can only be called on the UI thread.
//
// The non-static methods of this class use an internal cache. This cache is
// used to minimize IO->UI->IO round-trips of GetFrameIdOnIO. If the cost of
// attaching FrameTreeNode IDs to requests is negligible (crbug.com/524228),
// then we can remove all key caching and remove the cache from this class.
// TODO(robwu): Keep an eye on crbug.com/524228 and act upon the outcome.
class ExtensionApiFrameIdMap {
 public:
  using FrameIdCallback =
      base::Callback<void(int extension_api_frame_id,
                          int extension_api_parent_frame_id)>;

  // An invalid extension API frame ID.
  static const int kInvalidFrameId;

  static ExtensionApiFrameIdMap* Get();

  // Get the extension API frame ID for |rfh|.
  static int GetFrameId(content::RenderFrameHost* rfh);

  // Get the extension API frame ID for the parent of |rfh|.
  static int GetParentFrameId(content::RenderFrameHost* rfh);

  // Find the current RenderFrameHost for a given WebContents and extension
  // frame ID.
  // Returns nullptr if not found.
  static content::RenderFrameHost* GetRenderFrameHostById(
      content::WebContents* web_contents,
      int frame_id);

  // Runs |callback| with the result that is equivalent to calling GetFrameId()
  // on the UI thread. Thread hopping is minimized if possible. Callbacks for
  // the same |render_process_id| and |frame_routing_id| are guaranteed to be
  // run in order. The order of other callbacks is undefined.
  void GetFrameIdOnIO(int render_process_id,
                      int frame_routing_id,
                      const FrameIdCallback& callback);

  // Looks up the frame ID and stores it in the map. This method should be
  // called as early as possible, e.g. in a
  // WebContentsObserver::RenderFrameCreated notification.
  void CacheFrameId(content::RenderFrameHost* rfh);

  // Removes the frame ID mapping for a given frame. This method can be called
  // at any time, but it is typically called when a frame is destroyed.
  // If this method is not called, the cached mapping for the frame is retained
  // forever.
  void RemoveFrameId(content::RenderFrameHost* rfh);

 protected:
  friend struct base::DefaultLazyInstanceTraits<ExtensionApiFrameIdMap>;

  // A set of identifiers that uniquely identifies a RenderFrame.
  struct RenderFrameIdKey {
    RenderFrameIdKey();
    RenderFrameIdKey(int render_process_id, int frame_routing_id);

    // The process ID of the renderer that contains the RenderFrame.
    int render_process_id;

    // The routing ID of the RenderFrame.
    int frame_routing_id;

    bool operator<(const RenderFrameIdKey& other) const;
    bool operator==(const RenderFrameIdKey& other) const;
  };

  // The cached pair of frame IDs of the frame. Every RenderFrameIdKey
  // maps to a CachedFrameIdPair.
  struct CachedFrameIdPair {
    CachedFrameIdPair();
    CachedFrameIdPair(int frame_id, int parent_frame_id);

    // The extension API frame ID of the frame.
    int frame_id;

    // The extension API frame ID of the parent of the frame.
    int parent_frame_id;
  };

  struct FrameIdCallbacks {
    FrameIdCallbacks();
    ~FrameIdCallbacks();

    // This is a std::list so that iterators are not invalidated when the list
    // is modified during an iteration.
    std::list<FrameIdCallback> callbacks;

    // To avoid re-entrant processing of callbacks.
    bool is_iterating;
  };

  using FrameIdMap = std::map<RenderFrameIdKey, CachedFrameIdPair>;
  using FrameIdCallbacksMap = std::map<RenderFrameIdKey, FrameIdCallbacks>;

  ExtensionApiFrameIdMap();
  ~ExtensionApiFrameIdMap();

  // Determines the value to be stored in |frame_id_map_| for a given key. This
  // method is only called when |key| is not in |frame_id_map_|.
  // virtual for testing.
  virtual CachedFrameIdPair KeyToValue(const RenderFrameIdKey& key) const;

  CachedFrameIdPair LookupFrameIdOnUI(const RenderFrameIdKey& key);

  // Called as soon as the frame ID is found for the given |key|, and runs all
  // queued callbacks with |cached_frame_id_pair|.
  void ReceivedFrameIdOnIO(const RenderFrameIdKey& key,
                           const CachedFrameIdPair& cached_frame_id_pair);

  // Implementation of CacheFrameId(RenderFrameHost), separated for testing.
  void CacheFrameId(const RenderFrameIdKey& key);

  // Implementation of RemoveFrameId(RenderFrameHost), separated for testing.
  void RemoveFrameId(const RenderFrameIdKey& key);

  // Queued callbacks for use on the IO thread.
  FrameIdCallbacksMap callbacks_map_;

  // This map is only modified on the UI thread and is used to minimize the
  // number of thread hops on the IO thread.
  FrameIdMap frame_id_map_;

  // This lock protects |frame_id_map_| from being concurrently written on the
  // UI thread and read on the IO thread.
  base::Lock frame_id_map_lock_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionApiFrameIdMap);
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_EXTENSION_API_FRAME_ID_MAP_H_
