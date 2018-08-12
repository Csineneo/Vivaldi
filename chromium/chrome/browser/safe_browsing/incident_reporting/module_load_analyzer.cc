// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/safe_browsing/incident_reporting/module_load_analyzer.h"

#include "base/feature_list.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/safe_browsing/incident_reporting/incident_receiver.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "components/safe_browsing_db/database_manager.h"

namespace safe_browsing {

#if defined(OS_WIN)
// Enables analysis of suspicious modules loaded in the process.
extern const base::Feature kIncidentReportingModuleLoadAnalysis{
    "IncidentReportingModuleLoadAnalysis", base::FEATURE_DISABLED_BY_DEFAULT};
#endif

void RegisterModuleLoadAnalysis(
    const scoped_refptr<SafeBrowsingDatabaseManager>& database_manager) {
#if defined(OS_WIN)
  DCHECK(database_manager);
  if (base::FeatureList::IsEnabled(kIncidentReportingModuleLoadAnalysis)) {
    scoped_refptr<SafeBrowsingService> safe_browsing_service(
        g_browser_process->safe_browsing_service());

    if (safe_browsing_service) {
      safe_browsing_service
          ->RegisterExtendedReportingOnlyDelayedAnalysisCallback(
              base::Bind(&VerifyModuleLoadState, database_manager));
    }
  }
#endif
}

#if !defined(OS_WIN)
void VerifyModuleLoadState(
    const scoped_refptr<SafeBrowsingDatabaseManager>& database_manager,
    scoped_ptr<IncidentReceiver> incident_receiver) {}
#endif  // !defined(OS_WIN)

}  // namespace safe_browsing
