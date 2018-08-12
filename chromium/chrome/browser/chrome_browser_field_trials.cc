// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chrome_browser_field_trials.h"

#include <string>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram_base.h"
#include "base/metrics/persistent_histogram_allocator.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/metrics/chrome_metrics_service_client.h"
#include "chrome/common/chrome_switches.h"
#include "components/metrics/metrics_pref_names.h"

#if defined(OS_ANDROID)
#include "chrome/browser/chrome_browser_field_trials_mobile.h"
#else
#include "chrome/browser/chrome_browser_field_trials_desktop.h"
#endif

namespace {

// Check for feature enabling the use of persistent histogram storage and
// enable the global allocator if so.
void InstantiatePersistentHistograms() {
  if (base::FeatureList::IsEnabled(base::kPersistentHistogramsFeature)) {
    // Create persistent/shared memory and allow histograms to be stored in
    // it. Memory that is not actualy used won't be physically mapped by the
    // system. BrowserMetrics usage, as reported in UMA, peaked around 1.9MiB
    // as of 2016-02-20.
    base::GlobalHistogramAllocator::CreateWithLocalMemory(
        3 << 20,     // 3 MiB
        0x935DDD43,  // SHA1(BrowserMetrics)
        ChromeMetricsServiceClient::kBrowserMetricsName);
    base::GlobalHistogramAllocator* allocator =
        base::GlobalHistogramAllocator::Get();
    allocator->CreateTrackingHistograms(
        ChromeMetricsServiceClient::kBrowserMetricsName);
  }
}

}  // namespace

ChromeBrowserFieldTrials::ChromeBrowserFieldTrials(
    const base::CommandLine& parsed_command_line)
    : parsed_command_line_(parsed_command_line) {
}

ChromeBrowserFieldTrials::~ChromeBrowserFieldTrials() {
}

void ChromeBrowserFieldTrials::SetupFieldTrials() {
  // Field trials that are shared by all platforms.
  InstantiateDynamicTrials();

#if defined(OS_ANDROID)
  chrome::SetupMobileFieldTrials(parsed_command_line_);
#else
  chrome::SetupDesktopFieldTrials(parsed_command_line_);
#endif
}

void ChromeBrowserFieldTrials::InstantiateDynamicTrials() {
  // Persistent histograms must be enabled as soon as possible.
  InstantiatePersistentHistograms();
}
