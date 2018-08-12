// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/content_suggestions/content_suggestion_extra_builder.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@implementation ContentSuggestionReadingListExtra

+ (ContentSuggestionReadingListExtra*)extraWithDistillationStatus:
    (ReadingListUIDistillationStatus)status {
  ContentSuggestionReadingListExtra* extra =
      [[ContentSuggestionReadingListExtra alloc] init];
  extra.status = status;
  return extra;
}

@synthesize status = _status;

@end
