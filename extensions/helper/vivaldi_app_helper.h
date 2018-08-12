// Copyright (c) 2015 Vivaldi Technologies AS. All rights reserved.

#ifndef EXTENSIONS_HELPER_VIVALDI_APP_HELPER_H_
#define EXTENSIONS_HELPER_VIVALDI_APP_HELPER_H_

#include "content/public/browser/web_contents_user_data.h"

namespace extensions {

// Helper class to detect if current WebContent was created in app_window by
// Vivaldi app. I.e should only be called from Init function in app_window
// inside a IsVivaldiRunning() block
class VivaldiAppHelper :
  public content::WebContentsUserData<VivaldiAppHelper> {
public:
  ~VivaldiAppHelper() override;

private:
  explicit VivaldiAppHelper(content::WebContents *contents);
  friend class content::WebContentsUserData<VivaldiAppHelper>;

  DISALLOW_COPY_AND_ASSIGN(VivaldiAppHelper);
};

}  // namespace extensions

#endif  // EXTENSIONS_HELPER_VIVALDI_APP_HELPER_H_
