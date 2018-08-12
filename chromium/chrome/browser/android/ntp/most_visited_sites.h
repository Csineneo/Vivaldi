// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_NTP_MOST_VISITED_SITES_H_
#define CHROME_BROWSER_ANDROID_NTP_MOST_VISITED_SITES_H_

#include <stddef.h>

#include <memory>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observer.h"
#include "chrome/browser/android/ntp/popular_sites.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/top_sites_observer.h"
#include "components/suggestions/proto/suggestions.pb.h"
#include "components/suggestions/suggestions_service.h"
#include "url/gurl.h"

namespace history {
class TopSites;
}

namespace suggestions {
class SuggestionsService;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace variations {
class VariationsService;
}

// Shim interface for SupervisedUserService.
class MostVisitedSitesSupervisor {
 public:
  struct Whitelist {
    base::string16 title;
    GURL entry_point;
    base::FilePath large_icon_path;
  };

  class Observer {
   public:
    virtual void OnBlockedSitesChanged() = 0;

   protected:
    ~Observer() {}
  };

  // Pass non-null to set observer, or null to remove observer.
  // If setting observer, there must not yet be an observer set.
  // If removing observer, there must already be one to remove.
  // Does not take ownership. Observer must outlive this object.
  virtual void SetObserver(Observer* new_observer) = 0;

  // If true, |url| should not be shown on the NTP.
  virtual bool IsBlocked(const GURL& url) = 0;

  // Explicit suggestions for sites to show on NTP.
  virtual std::vector<Whitelist> whitelists() = 0;

  // If true, be conservative about suggesting sites from outside sources.
  virtual bool IsChildProfile() = 0;

 protected:
  virtual ~MostVisitedSitesSupervisor() {}
};

// Tracks the list of most visited sites and their thumbnails.
//
// Do not use, except from MostVisitedSitesBridge. The interface is in flux
// while we are extracting the functionality of the Java class to make available
// in C++.
//
// TODO(sfiera): finalize interface.
class MostVisitedSites : public history::TopSitesObserver,
                         public MostVisitedSitesSupervisor::Observer {
 public:
  struct Suggestion;
  using SuggestionsVector = std::vector<Suggestion>;
  using PopularSitesVector = std::vector<PopularSites::Site>;

  // The source of the Most Visited sites.
  enum MostVisitedSource { TOP_SITES, SUGGESTIONS_SERVICE, POPULAR, WHITELIST };

  // The observer to be notified when the list of most visited sites changes.
  class Observer {
   public:
    virtual void OnMostVisitedURLsAvailable(
        const SuggestionsVector& suggestions) = 0;
    virtual void OnPopularURLsAvailable(const PopularSitesVector& sites) = 0;

   protected:
    virtual ~Observer() {}
  };

  struct Suggestion {
    base::string16 title;
    GURL url;
    MostVisitedSource source;

    // Only valid for source == WHITELIST (empty otherwise).
    base::FilePath whitelist_icon_path;

    // Only valid for source == SUGGESTIONS_SERVICE (-1 otherwise).
    int provider_index;

    Suggestion();
    ~Suggestion();

    Suggestion(Suggestion&&);
    Suggestion& operator=(Suggestion&&);

   private:
    DISALLOW_COPY_AND_ASSIGN(Suggestion);
  };

  MostVisitedSites(PrefService* prefs,
                   const TemplateURLService* template_url_service,
                   variations::VariationsService* variations_service,
                   net::URLRequestContextGetter* download_context,
                   const base::FilePath& popular_sites_directory,
                   scoped_refptr<history::TopSites> top_sites,
                   suggestions::SuggestionsService* suggestions,
                   MostVisitedSitesSupervisor* supervisor);

  ~MostVisitedSites() override;

  // Does not take ownership of |observer|, which must outlive this object and
  // must not be null.
  void SetMostVisitedURLsObserver(Observer* observer, int num_sites);

  using ThumbnailCallback = base::Callback<
      void(bool /* is_local_thumbnail */, const SkBitmap* /* bitmap */)>;
  void GetURLThumbnail(const GURL& url, const ThumbnailCallback& callback);
  void AddOrRemoveBlacklistedUrl(const GURL& url, bool add_url);
  void RecordTileTypeMetrics(const std::vector<int>& tile_types);
  void RecordOpenedMostVisitedItem(int index, int tile_type);

  // MostVisitedSitesSupervisor::Observer implementation.
  void OnBlockedSitesChanged() override;

  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

 private:
  friend class MostVisitedSitesTest;

  // TODO(treib): use SuggestionsVector in internal functions. crbug.com/601734
  using SuggestionsPtrVector = std::vector<std::unique_ptr<Suggestion>>;

  void BuildCurrentSuggestions();

  // Initialize the query to Top Sites. Called if the SuggestionsService
  // returned no data.
  void InitiateTopSitesQuery();

  // If there's a whitelist entry point for the URL, return the large icon path.
  base::FilePath GetWhitelistLargeIconPath(const GURL& url);

  // Callback for when data is available from TopSites.
  void OnMostVisitedURLsAvailable(
      const history::MostVisitedURLList& visited_list);

  // Callback for when data is available from the SuggestionsService.
  void OnSuggestionsProfileAvailable(
      const suggestions::SuggestionsProfile& suggestions_profile);

  // Takes the personal suggestions and creates whitelist entry point
  // suggestions if necessary.
  SuggestionsPtrVector CreateWhitelistEntryPointSuggestions(
      const SuggestionsPtrVector& personal_suggestions);

  // Takes the personal and whitelist suggestions and creates popular
  // suggestions if necessary.
  SuggestionsPtrVector CreatePopularSitesSuggestions(
      const SuggestionsPtrVector& personal_suggestions,
      const SuggestionsPtrVector& whitelist_suggestions);

  // Takes the personal suggestions, creates and merges in whitelist and popular
  // suggestions if appropriate, and saves the new suggestions.
  void SaveNewSuggestions(SuggestionsPtrVector* personal_suggestions);

  // Workhorse for SaveNewSuggestions above. Implemented as a separate static
  // method for ease of testing.
  static SuggestionsPtrVector MergeSuggestions(
      SuggestionsPtrVector* personal_suggestions,
      SuggestionsPtrVector* whitelist_suggestions,
      SuggestionsPtrVector* popular_suggestions);

  // Appends suggestions from |src| to |dst|.
  static void AppendSuggestions(SuggestionsPtrVector* src,
                                SuggestionsPtrVector* dst);

  void SaveCurrentSuggestionsToPrefs();

  // Notifies the observer about the availability of suggestions.
  // Also records impressions UMA if not done already.
  void NotifyMostVisitedURLsObserver();

  void OnPopularSitesAvailable(bool success);

  // Runs on the UI Thread.
  void OnLocalThumbnailFetched(
      const GURL& url,
      const ThumbnailCallback& callback,
      std::unique_ptr<SkBitmap> bitmap);

  // Callback for when the thumbnail lookup is complete.
  // Runs on the UI Thread.
  void OnObtainedThumbnail(
      bool is_local_thumbnail,
      const ThumbnailCallback& callback,
      const GURL& url,
      const SkBitmap* bitmap);

  // Records thumbnail-related UMA histogram metrics.
  void RecordThumbnailUMAMetrics();

  // Records UMA histogram metrics related to the number of impressions.
  void RecordImpressionUMAMetrics();

  // history::TopSitesObserver implementation.
  void TopSitesLoaded(history::TopSites* top_sites) override;
  void TopSitesChanged(history::TopSites* top_sites,
                       ChangeReason change_reason) override;

  PrefService* prefs_;
  const TemplateURLService* template_url_service_;
  variations::VariationsService* variations_service_;
  net::URLRequestContextGetter* download_context_;
  base::FilePath popular_sites_directory_;
  scoped_refptr<history::TopSites> top_sites_;
  suggestions::SuggestionsService* suggestions_service_;
  MostVisitedSitesSupervisor* supervisor_;

  Observer* observer_;

  // The maximum number of most visited sites to return.
  int num_sites_;

  // Whether we have received an initial set of most visited sites (from either
  // TopSites or the SuggestionsService).
  bool received_most_visited_sites_;

  // Whether we have received the set of popular sites. Immediately set to true
  // if popular sites are disabled.
  bool received_popular_sites_;

  // Whether we have recorded one-shot UMA metrics such as impressions. They are
  // recorded once both the previous flags are true.
  bool recorded_uma_;

  std::unique_ptr<
      suggestions::SuggestionsService::ResponseCallbackList::Subscription>
      suggestions_subscription_;

  ScopedObserver<history::TopSites, history::TopSitesObserver> scoped_observer_;

  MostVisitedSource mv_source_;

  std::unique_ptr<PopularSites> popular_sites_;

  SuggestionsVector current_suggestions_;

  // For callbacks may be run after destruction.
  base::WeakPtrFactory<MostVisitedSites> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(MostVisitedSites);
};

#endif  // CHROME_BROWSER_ANDROID_NTP_MOST_VISITED_SITES_H_
