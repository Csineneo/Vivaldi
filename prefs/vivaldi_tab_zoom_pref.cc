// Copyright (c) 2016 Vivaldi Technologies AS. All rights reserved.

#include "prefs/vivaldi_tab_zoom_pref.h"

#include "app/vivaldi_apptools.h"
#include "content/public/browser/web_contents_user_data.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "prefs/vivaldi_pref_names.h"

// NOTE(arnar@vivaldi.com): Helper function to determine if Vivaldi tab zoom
// is enabled.
namespace vivaldi {

bool isTabZoomEnabled(content::WebContents *web_contents) {
  Profile* profile = Profile::FromBrowserContext(
    web_contents->GetBrowserContext());

  bool is_vivaldi = ::vivaldi::IsVivaldiRunning();
  if (!is_vivaldi)
    return false;

  bool tabZoom = profile->GetPrefs()->GetBoolean(vivaldiprefs::kVivaldiTabZoom);
  return tabZoom;
}

}  // namespace vivaldi
