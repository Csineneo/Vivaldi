//
// Copyright (c) 2016 Vivaldi Technologies AS. All rights reserved.
//
#import <Cocoa/Cocoa.h>

#include "base/task/cancelable_task_tracker.h"

namespace favicon {
class FaviconService;
}

namespace favicon_base {
struct FaviconImageResult;
}

class Profile;

namespace vivaldi {

class FaviconLoaderMac {
 public:
  FaviconLoaderMac(Profile* profile);
  ~FaviconLoaderMac();

  void LoadFavicon(NSMenuItem *item, const std::string& url);
  void OnFaviconDataAvailable(NSMenuItem* item,
    const favicon_base::FaviconImageResult& image_result);
  void CancelPendingRequests();

 private:
  std::unique_ptr<base::CancelableTaskTracker> cancelable_task_tracker_;
  favicon::FaviconService* favicon_service_;
  Profile* profile_;
};

}  // namespace vivaldi
