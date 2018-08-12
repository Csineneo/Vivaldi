// Copyright (c) 2015 Vivaldi Technologies AS. All rights reserved
#include "testing/disable_unittests_macros.h"

// List of unittest disabled for all platforms  by Vivaldi
// On the form
//    DISABLE(foo,bar)
//    DISABLE(foo,baz)

  // In Vivaldi this will always break since the relevant code has been modified
  DISABLE(BrowserOptionsWebUITest, testOpenBrowserOptions)

  // In Vivaldi this will always break since the relevant code has been modified
  DISABLE(BrowserOptionsOverlayWebUITest, testNavigationInBackground)
  // ******

  // Nacl is disabled in Vivaldi
  DISABLE_ALL(ChromeServiceWorkerFetchPPAPITest)
  DISABLE_ALL(ChromeServiceWorkerFetchPPAPIPrivateTest)

  // In Vivaldi this will always break since the relevant code has been modified
  DISABLE(ExtensionApiTest, StubsApp)

  // internal HTML page modified, breaking the test
  DISABLE(HelpPageWebUITest, testOpenHelpPage)

  // never stop loading; disabled widevine pepper support(?)
  DISABLE(PepperContentSettingsSpecialCasesJavaScriptBlockedTest, WidevineCdm)

  DISABLE(UberUIBrowserTest, EnableMdExtensionsHidesExtensions)
  DISABLE(UberUIBrowserTest, EnableMdHistoryHidesHistory)
  DISABLE(UberUIBrowserTest, EnableSettingsWindowHidesSettingsAndHelp)
  DISABLE(UberUIBrowserTest, EnableMdSettingsHidesSettings)

  DISABLE(WebUIAccessibilityAuditBrowserTest_IssuesAreWarnings,
          testCanIgnoreSelectors)
  DISABLE(WebUIAccessibilityAuditBrowserTest_IssuesAreWarnings,
          testWithAuditFailuresAndExpectA11yOk)

  DISABLE(WebUIAccessibilityAuditBrowserTest_TestsDisabledInFixture,
          testRunningAuditManually_noErrors)
  DISABLE(WebUIAccessibilityAuditBrowserTest_TestsDisabledInFixture,
          testRunningAuditManuallySeveralTimes)

  // Found flaky when looking at VB-13454. The order of downloads requested is
  // not necessarily the same as they get processed. See bug for log.
  DISABLE_MULTI(WebViewTest, DownloadPermission)

  // In Vivaldi this will always break since the relevant code has been modified
  DISABLE(WebViewTest, Shim_TestRendererNavigationRedirectWhileUnattached)
  DISABLE(BrowsingHistoryHandlerTest, ObservingWebHistoryDeletions)

  // Failing media tests since proprietary media code was imported
  DISABLE(AudioVideoMetadataExtractorTest, AndroidRotatedMP4Video)
  DISABLE(AudioVideoMetadataExtractorTest, AudioMP3)
  DISABLE(MediaGalleriesPlatformAppBrowserTest, GetMetadata)
  DISABLE_MULTI(MediaTest, VideoBearMovPcmS16be)
  DISABLE_MULTI(MediaTest, VideoBearMovPcmS24be)
  DISABLE_MULTI(MediaTest, VideoBearMp4)
  DISABLE_MULTI(MediaTest, VideoBearSilentMp4)
  DISABLE(MediaTest, VideoBearRotated0)
  DISABLE(MediaTest, VideoBearRotated180)
  DISABLE(MediaColorTest, Yuv420pHighBitDepth)
  DISABLE(MediaColorTest, Yuv422pH264)
  DISABLE(MediaColorTest, Yuv444pH264)
  DISABLE(MediaColorTest, Yuvj420pH264)
