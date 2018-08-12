// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SSL_SSL_MANAGER_H_
#define CONTENT_BROWSER_SSL_SSL_MANAGER_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "content/browser/ssl/ssl_error_handler.h"
#include "content/common/content_export.h"
#include "content/public/browser/global_request_id.h"
#include "net/base/net_errors.h"
#include "net/cert/cert_status_flags.h"
#include "url/gurl.h"

namespace net {
class SSLInfo;
}

namespace content {
class BrowserContext;
class NavigationEntryImpl;
class NavigationControllerImpl;
class SSLHostStateDelegate;
struct LoadCommittedDetails;

// The SSLManager controls the SSL UI elements in a WebContents.  It
// listens for various events that influence when these elements should or
// should not be displayed and adjusts them accordingly.
//
// There is one SSLManager per tab.
// The security state (secure/insecure) is stored in the navigation entry.
// Along with it are stored any SSL error code and the associated cert.
class CONTENT_EXPORT SSLManager {
 public:
  // Entry point for SSLCertificateErrors.  This function begins the process
  // of resolving a certificate error during an SSL connection.  SSLManager
  // will adjust the security UI and either call |CancelSSLRequest| or
  // |ContinueSSLRequest| of |delegate|.
  //
  // Called on the IO thread.
  static void OnSSLCertificateError(
      const base::WeakPtr<SSLErrorHandler::Delegate>& delegate,
      ResourceType resource_type,
      const GURL& url,
      const base::Callback<WebContents*(void)>& web_contents_getter,
      const net::SSLInfo& ssl_info,
      bool fatal);

  // Same as the above, and only works for subresources. Prefer using
  // OnSSLCertificateError whenever possible (ie when you have access to the
  // WebContents).
  static void OnSSLCertificateSubresourceError(
      const base::WeakPtr<SSLErrorHandler::Delegate>& delegate,
      const GURL& url,
      int render_process_id,
      int render_frame_id,
      const net::SSLInfo& ssl_info,
      bool fatal);

  // Called when SSL state for a host or tab changes.
  static void NotifySSLInternalStateChanged(BrowserContext* context);

  // Construct an SSLManager for the specified tab.
  explicit SSLManager(NavigationControllerImpl* controller);
  virtual ~SSLManager();

  // The navigation controller associated with this SSLManager.  The
  // NavigationController is guaranteed to outlive the SSLManager.
  NavigationControllerImpl* controller() { return controller_; }

  void DidCommitProvisionalLoad(const LoadCommittedDetails& details);
  void DidStartResourceResponse(const GURL& url,
                                bool has_certificate,
                                net::CertStatus ssl_cert_status);

  // Entry point for insecure mixed content (loaded over HTTP).
  void DidRunInsecureContent(const GURL& security_origin);

  // Entry point for content loaded with HTTPS certificate errors.
  void DidRunContentWithCertErrors(const GURL& security_origin);

  // An error occurred with the certificate in an SSL connection.
  void OnCertError(std::unique_ptr<SSLErrorHandler> handler);

 private:
  enum OnCertErrorInternalOptionsMask {
    OVERRIDABLE = 1 << 0,
    STRICT_ENFORCEMENT = 1 << 1,
    EXPIRED_PREVIOUS_DECISION = 1 << 2
  };

  // Helper method for handling certificate errors.
  //
  // Options should be a bitmask combination of OnCertErrorInternalOptionsMask.
  // OVERRIDABLE indicates whether or not the user could (assuming perfect
  // knowledge) successfully override the error and still get the security
  // guarantees of TLS. STRICT_ENFORCEMENT indicates whether or not the site the
  // user is trying to connect to has requested strict enforcement of
  // certificate validation (e.g. with HTTP Strict-Transport-Security).
  // EXPIRED_PREVIOUS_DECISION indicates whether a user decision had been
  // previously made but the decision has expired.
  void OnCertErrorInternal(std::unique_ptr<SSLErrorHandler> handler,
                           int options_mask);

  // Updates the NavigationEntry with our current state. This will
  // notify the WebContents of an SSL state change if a change was
  // actually made.
  void UpdateEntry(NavigationEntryImpl* entry);

  // Notifies the WebContents that the SSL state changed.
  void NotifyDidChangeVisibleSSLState();

  // The NavigationController that owns this SSLManager.  We are responsible
  // for the security UI of this tab.
  NavigationControllerImpl* controller_;

  // Delegate that manages SSL state specific to each host.
  SSLHostStateDelegate* ssl_host_state_delegate_;

  DISALLOW_COPY_AND_ASSIGN(SSLManager);
};

}  // namespace content

#endif  // CONTENT_BROWSER_SSL_SSL_MANAGER_H_
