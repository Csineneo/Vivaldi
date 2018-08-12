// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web_view/internal/translate/cwv_translate_manager_impl.h"

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "components/translate/core/browser/translate_manager.h"
#include "components/translate/core/browser/translate_ui_delegate.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@implementation CWVTranslateManagerImpl {
  std::unique_ptr<translate::TranslateUIDelegate> _translateUIDelegate;
}

- (instancetype)initWithTranslateManager:(translate::TranslateManager*)manager
                          sourceLanguage:(const std::string&)source
                          targetLanguage:(const std::string&)target {
  if ((self = [super init])) {
    DCHECK(manager);
    _translateUIDelegate = base::MakeUnique<translate::TranslateUIDelegate>(
        manager->GetWeakPtr(), source, target);
  }
  return self;
}

#pragma mark CWVTranslateManager methods

- (void)translate {
  _translateUIDelegate->Translate();
}

- (void)revertTranslation {
  _translateUIDelegate->RevertTranslation();
}

@end
