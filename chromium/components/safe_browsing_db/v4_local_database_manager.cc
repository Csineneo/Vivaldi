// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file should not be build on Android but is currently getting built.
// TODO(vakh): Fix that: http://crbug.com/621647

#include "components/safe_browsing_db/v4_local_database_manager.h"

#include <vector>

#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "components/safe_browsing_db/v4_feature_list.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace safe_browsing {

namespace {

const ThreatSeverity kLeastSeverity =
    std::numeric_limits<ThreatSeverity>::max();

ListInfos GetListInfos() {
  return ListInfos(
      {ListInfo(true, "UrlMalware.store", GetUrlMalwareId(),
                SB_THREAT_TYPE_URL_MALWARE),
       ListInfo(true, "UrlSoceng.store", GetUrlSocEngId(),
                SB_THREAT_TYPE_URL_PHISHING),
       ListInfo(false, "", GetChromeUrlApiId(), SB_THREAT_TYPE_API_ABUSE)});
}

// Returns the severity information about a given SafeBrowsing list. The lowest
// value is 0, which represents the most severe list.
ThreatSeverity GetThreatSeverity(const ListIdentifier& list_id) {
  switch (list_id.threat_type()) {
    case MALWARE_THREAT:
    case SOCIAL_ENGINEERING_PUBLIC:
      return 0;
    case API_ABUSE:
      return 1;
    default:
      NOTREACHED() << "Unexpected ThreatType encountered in GetThreatSeverity";
      return kLeastSeverity;
  }
}

}  // namespace

V4LocalDatabaseManager::PendingCheck::PendingCheck(
    Client* client,
    ClientCallbackType client_callback_type,
    const StoresToCheck& stores_to_check,
    const GURL& url)
    : client(client),
      client_callback_type(client_callback_type),
      result_threat_type(SB_THREAT_TYPE_SAFE),
      stores_to_check(stores_to_check),
      url(url) {
  DCHECK_GT(ClientCallbackType::CHECK_MAX, client_callback_type);
}

V4LocalDatabaseManager::PendingCheck::~PendingCheck() {}

// static
scoped_refptr<V4LocalDatabaseManager> V4LocalDatabaseManager::Create(
    const base::FilePath& base_path) {
  if (!V4FeatureList::IsLocalDatabaseManagerEnabled()) {
    return nullptr;
  }

  return make_scoped_refptr(new V4LocalDatabaseManager(base_path));
}

V4LocalDatabaseManager::V4LocalDatabaseManager(const base::FilePath& base_path)
    : base_path_(base_path),
      enabled_(false),
      list_infos_(GetListInfos()),
      weak_factory_(this) {
  DCHECK(!base_path_.empty());
  DCHECK(!list_infos_.empty());

  DVLOG(1) << "V4LocalDatabaseManager::V4LocalDatabaseManager: "
           << "base_path_: " << base_path_.AsUTF8Unsafe();
}

V4LocalDatabaseManager::~V4LocalDatabaseManager() {
  DCHECK(!enabled_);
}

//
// Start: SafeBrowsingDatabaseManager implementation
//

void V4LocalDatabaseManager::CancelCheck(Client* client) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK(enabled_);

  auto it = pending_clients_.find(client);
  if (it != pending_clients_.end()) {
    pending_clients_.erase(it);
  }

  auto queued_it =
      std::find_if(std::begin(queued_checks_), std::end(queued_checks_),
                   [&client](const std::unique_ptr<PendingCheck>& check) {
                     return check->client == client;
                   });
  if (queued_it != queued_checks_.end()) {
    queued_checks_.erase(queued_it);
  }
}

bool V4LocalDatabaseManager::CanCheckResourceType(
    content::ResourceType resource_type) const {
  // We check all types since most checks are fast.
  return true;
}

bool V4LocalDatabaseManager::CanCheckUrl(const GURL& url) const {
  return url.SchemeIs(url::kHttpsScheme) || url.SchemeIs(url::kHttpScheme) ||
         url.SchemeIs(url::kFtpScheme);
}

bool V4LocalDatabaseManager::ChecksAreAlwaysAsync() const {
  return false;
}

bool V4LocalDatabaseManager::CheckBrowseUrl(const GURL& url, Client* client) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (!enabled_ || !CanCheckUrl(url)) {
    return true;
  }

  std::unique_ptr<PendingCheck> check = base::MakeUnique<PendingCheck>(
      client, ClientCallbackType::CHECK_BROWSE_URL,
      StoresToCheck({GetUrlMalwareId(), GetUrlSocEngId()}), url);
  if (!v4_database_) {
    queued_checks_.push_back(std::move(check));
    return false;
  }

  FullHashToStoreAndHashPrefixesMap full_hash_to_store_and_hash_prefixes;
  if (!GetPrefixMatches(check, &full_hash_to_store_and_hash_prefixes)) {
    return true;
  }

  PerformFullHashCheck(std::move(check), full_hash_to_store_and_hash_prefixes);
  return false;
}

bool V4LocalDatabaseManager::CheckDownloadUrl(
    const std::vector<GURL>& url_chain,
    Client* client) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  // TODO(vakh): Implement this skeleton.
  return true;
}

bool V4LocalDatabaseManager::CheckExtensionIDs(
    const std::set<std::string>& extension_ids,
    Client* client) {
  // TODO(vakh): Implement this skeleton.
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  return true;
}

bool V4LocalDatabaseManager::CheckResourceUrl(const GURL& url, Client* client) {
  // TODO(vakh): Implement this skeleton.
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  return true;
}

bool V4LocalDatabaseManager::MatchCsdWhitelistUrl(const GURL& url) {
  // TODO(vakh): Implement this skeleton.
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  return true;
}

bool V4LocalDatabaseManager::MatchDownloadWhitelistString(
    const std::string& str) {
  // TODO(vakh): Implement this skeleton.
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  return true;
}

bool V4LocalDatabaseManager::MatchDownloadWhitelistUrl(const GURL& url) {
  // TODO(vakh): Implement this skeleton.
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  return true;
}

bool V4LocalDatabaseManager::MatchMalwareIP(const std::string& ip_address) {
  // TODO(vakh): Implement this skeleton.
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  return false;
}

bool V4LocalDatabaseManager::MatchModuleWhitelistString(
    const std::string& str) {
  // TODO(vakh): Implement this skeleton.
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  return true;
}

ThreatSource V4LocalDatabaseManager::GetThreatSource() const {
  return ThreatSource::LOCAL_PVER4;
}

bool V4LocalDatabaseManager::IsCsdWhitelistKillSwitchOn() {
  // TODO(vakh): Implement this skeleton.
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  return true;
}

bool V4LocalDatabaseManager::IsDownloadProtectionEnabled() const {
  // TODO(vakh): Investigate the possibility of using a command line switch for
  // this instead.
  return true;
}

bool V4LocalDatabaseManager::IsMalwareKillSwitchOn() {
  // TODO(vakh): Implement this skeleton.
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  return true;
}

bool V4LocalDatabaseManager::IsSupported() const {
  return true;
}

void V4LocalDatabaseManager::StartOnIOThread(
    net::URLRequestContextGetter* request_context_getter,
    const V4ProtocolConfig& config) {
  SafeBrowsingDatabaseManager::StartOnIOThread(request_context_getter, config);

  db_updated_callback_ = base::Bind(&V4LocalDatabaseManager::DatabaseUpdated,
                                    weak_factory_.GetWeakPtr());

  SetupUpdateProtocolManager(request_context_getter, config);
  SetupDatabase();

  enabled_ = true;
}

void V4LocalDatabaseManager::StopOnIOThread(bool shutdown) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  enabled_ = false;

  pending_clients_.clear();

  RespondSafeToQueuedChecks();

  // Delete the V4Database. Any pending writes to disk are completed.
  // This operation happens on the task_runner on which v4_database_ operates
  // and doesn't block the IO thread.
  V4Database::Destroy(std::move(v4_database_));

  // Delete the V4UpdateProtocolManager.
  // This cancels any in-flight update request.
  v4_update_protocol_manager_.reset();

  db_updated_callback_.Reset();

  SafeBrowsingDatabaseManager::StopOnIOThread(shutdown);
}

//
// End: SafeBrowsingDatabaseManager implementation
//

void V4LocalDatabaseManager::DatabaseReady(
    std::unique_ptr<V4Database> v4_database) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  // The following check is needed because it is possible that by the time the
  // database is ready, StopOnIOThread has been called.
  if (enabled_) {
    v4_database_ = std::move(v4_database);

    // The database is in place. Start fetching updates now.
    v4_update_protocol_manager_->ScheduleNextUpdate(
        v4_database_->GetStoreStateMap());

    ProcessQueuedChecks();
  } else {
    // Schedule the deletion of v4_database off IO thread.
    V4Database::Destroy(std::move(v4_database));
  }
}

void V4LocalDatabaseManager::DatabaseUpdated() {
  if (enabled_) {
    v4_update_protocol_manager_->ScheduleNextUpdate(
        v4_database_->GetStoreStateMap());
  }
}

bool V4LocalDatabaseManager::GetPrefixMatches(
    const std::unique_ptr<PendingCheck>& check,
    FullHashToStoreAndHashPrefixesMap* full_hash_to_store_and_hash_prefixes) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  DCHECK(enabled_);
  DCHECK(v4_database_);
  DCHECK_GT(ClientCallbackType::CHECK_MAX, check->client_callback_type);

  if (check->client_callback_type == ClientCallbackType::CHECK_BROWSE_URL) {
    std::unordered_set<FullHash> full_hashes;
    V4ProtocolManagerUtil::UrlToFullHashes(check->url, &full_hashes);

    StoreAndHashPrefixes matched_store_and_hash_prefixes;
    for (const auto& full_hash : full_hashes) {
      matched_store_and_hash_prefixes.clear();
      v4_database_->GetStoresMatchingFullHash(full_hash, check->stores_to_check,
                                              &matched_store_and_hash_prefixes);
      if (!matched_store_and_hash_prefixes.empty()) {
        (*full_hash_to_store_and_hash_prefixes)[full_hash] =
            matched_store_and_hash_prefixes;
      }
    }

    // No hash prefixes found in the local database so that resource must be
    // safe.
    return !full_hash_to_store_and_hash_prefixes->empty();
  }

  NOTREACHED() << "Unexpected client_callback_type encountered";
  return false;
}

void V4LocalDatabaseManager::GetSeverestThreatTypeAndMetadata(
    SBThreatType* result_threat_type,
    ThreatMetadata* metadata,
    const std::vector<FullHashInfo>& full_hash_infos) {
  DCHECK(result_threat_type);
  DCHECK(metadata);

  ThreatSeverity most_severe_yet = kLeastSeverity;
  for (const FullHashInfo& fhi : full_hash_infos) {
    ThreatSeverity severity = GetThreatSeverity(fhi.list_id);
    if (severity < most_severe_yet) {
      most_severe_yet = severity;
      *result_threat_type = GetSBThreatTypeForList(fhi.list_id);
      *metadata = fhi.metadata;
    }
  }
}

StoresToCheck V4LocalDatabaseManager::GetStoresForFullHashRequests() {
  StoresToCheck stores_for_full_hash;
  for (auto it : list_infos_) {
    stores_for_full_hash.insert(it.list_id());
  }
  return stores_for_full_hash;
}

// Returns the SBThreatType corresponding to a given SafeBrowsing list.
SBThreatType V4LocalDatabaseManager::GetSBThreatTypeForList(
    const ListIdentifier& list_id) {
  auto it = std::find_if(
      std::begin(list_infos_), std::end(list_infos_),
      [&list_id](ListInfo const& li) { return li.list_id() == list_id; });
  DCHECK(list_infos_.end() != it);
  DCHECK_NE(SB_THREAT_TYPE_SAFE, it->sb_threat_type());
  return it->sb_threat_type();
}

void V4LocalDatabaseManager::OnFullHashResponse(
    std::unique_ptr<PendingCheck> pending_check,
    const std::vector<FullHashInfo>& full_hash_infos) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (!enabled_) {
    DCHECK(pending_clients_.empty());
    return;
  }

  auto it = pending_clients_.find(pending_check->client);
  if (it == pending_clients_.end()) {
    // The check has since been cancelled.
    return;
  }

  // Find out the most severe threat, if any, to report to the client.
  GetSeverestThreatTypeAndMetadata(&pending_check->result_threat_type,
                                   &pending_check->url_metadata,
                                   full_hash_infos);
  pending_clients_.erase(it);
  RespondToClient(std::move(pending_check));
}

void V4LocalDatabaseManager::PerformFullHashCheck(
    std::unique_ptr<PendingCheck> check,
    const FullHashToStoreAndHashPrefixesMap&
        full_hash_to_store_and_hash_prefixes) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  DCHECK(enabled_);
  DCHECK(!full_hash_to_store_and_hash_prefixes.empty());

  pending_clients_.insert(check->client);

  v4_get_hash_protocol_manager_->GetFullHashes(
      full_hash_to_store_and_hash_prefixes,
      base::Bind(&V4LocalDatabaseManager::OnFullHashResponse,
                 weak_factory_.GetWeakPtr(), base::Passed(std::move(check))));
}

void V4LocalDatabaseManager::ProcessQueuedChecks() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  for (auto& it : queued_checks_) {
    FullHashToStoreAndHashPrefixesMap full_hash_to_store_and_hash_prefixes;
    if (!GetPrefixMatches(it, &full_hash_to_store_and_hash_prefixes)) {
      RespondToClient(std::move(it));
    } else {
      PerformFullHashCheck(std::move(it), full_hash_to_store_and_hash_prefixes);
    }
  }
  queued_checks_.clear();
}

void V4LocalDatabaseManager::RespondSafeToQueuedChecks() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  for (std::unique_ptr<PendingCheck>& it : queued_checks_) {
    RespondToClient(std::move(it));
  }
  queued_checks_.clear();
}

void V4LocalDatabaseManager::RespondToClient(
    std::unique_ptr<PendingCheck> pending_check) {
  DCHECK(pending_check.get());
  DCHECK_EQ(ClientCallbackType::CHECK_BROWSE_URL,
            pending_check->client_callback_type);
  // TODO(vakh): Implement this skeleton.
}

void V4LocalDatabaseManager::SetupDatabase() {
  DCHECK(!base_path_.empty());
  DCHECK(!list_infos_.empty());
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  // Only get a new task runner if there isn't one already. If the service has
  // previously been started and stopped, a task runner could already exist.
  if (!task_runner_) {
    base::SequencedWorkerPool* pool = BrowserThread::GetBlockingPool();
    task_runner_ = pool->GetSequencedTaskRunnerWithShutdownBehavior(
        pool->GetSequenceToken(), base::SequencedWorkerPool::SKIP_ON_SHUTDOWN);
  }

  // Do not create the database on the IO thread since this may be an expensive
  // operation. Instead, do that on the task_runner and when the new database
  // has been created, swap it out on the IO thread.
  NewDatabaseReadyCallback db_ready_callback = base::Bind(
      &V4LocalDatabaseManager::DatabaseReady, weak_factory_.GetWeakPtr());
  V4Database::Create(task_runner_, base_path_, list_infos_, db_ready_callback);
}

void V4LocalDatabaseManager::SetupUpdateProtocolManager(
    net::URLRequestContextGetter* request_context_getter,
    const V4ProtocolConfig& config) {
  V4UpdateCallback callback =
      base::Bind(&V4LocalDatabaseManager::UpdateRequestCompleted,
                 weak_factory_.GetWeakPtr());

  v4_update_protocol_manager_ =
      V4UpdateProtocolManager::Create(request_context_getter, config, callback);
}

void V4LocalDatabaseManager::UpdateRequestCompleted(
    std::unique_ptr<ParsedServerResponse> parsed_server_response) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  v4_database_->ApplyUpdate(std::move(parsed_server_response),
                            db_updated_callback_);
}

}  // namespace safe_browsing
