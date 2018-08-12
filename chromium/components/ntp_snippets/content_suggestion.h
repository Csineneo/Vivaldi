// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_NTP_SNIPPETS_CONTENT_SUGGESTION_H_
#define COMPONENTS_NTP_SNIPPETS_CONTENT_SUGGESTION_H_

#include <string>

#include "base/macros.h"
#include "base/strings/string16.h"
#include "base/time/time.h"
#include "components/ntp_snippets/category.h"
#include "url/gurl.h"

namespace ntp_snippets {

// A content suggestion for the new tab page, which can be an article or an
// offline page, for example.
class ContentSuggestion {
 public:
  class ID {
   public:
    ID(Category category, const std::string& id_within_category)
        : category_(category), id_within_category_(id_within_category) {}

    Category category() const { return category_; }

    const std::string& id_within_category() const {
      return id_within_category_;
    }

    bool operator==(const ID& rhs) const;
    bool operator!=(const ID& rhs) const;

   private:
    Category category_;
    std::string id_within_category_;

    // Allow copy and assignment.
  };

  // Creates a new ContentSuggestion. The caller must ensure that the |id|
  // passed in here is unique application-wide.
  ContentSuggestion(ID id, const GURL& url);
  ContentSuggestion(Category category,
                    const std::string& id_within_category,
                    const GURL& url);
  ContentSuggestion(ContentSuggestion&&);
  ContentSuggestion& operator=(ContentSuggestion&&);

  ~ContentSuggestion();

  // An ID for identifying the suggestion. The ID is unique application-wide.
  const ID& id() const { return id_; }

  // The normal content URL where the content referenced by the suggestion can
  // be accessed.
  const GURL& url() const { return url_; }

  // If available, this contains an URL to an AMP version of the same content.
  // Otherwise, this is an empty GURL().
  const GURL& amp_url() const { return amp_url_; }
  void set_amp_url(const GURL& amp_url) { amp_url_ = amp_url; }

  // Title of the suggestion.
  const base::string16& title() const { return title_; }
  void set_title(const base::string16& title) { title_ = title; }

  // Summary or relevant textual extract from the content.
  const base::string16& snippet_text() const { return snippet_text_; }
  void set_snippet_text(const base::string16& snippet_text) {
    snippet_text_ = snippet_text;
  }

  // The time when the content represented by this suggestion was published.
  const base::Time& publish_date() const { return publish_date_; }
  void set_publish_date(const base::Time& publish_date) {
    publish_date_ = publish_date;
  }

  // The name of the source/publisher of this suggestion.
  const base::string16& publisher_name() const { return publisher_name_; }
  void set_publisher_name(const base::string16& publisher_name) {
    publisher_name_ = publisher_name;
  }

  // TODO(pke): Remove the score from the ContentSuggestion class. The UI only
  // uses it to track user clicks (histogram data). Instead, the providers
  // should be informed about clicks and do appropriate logging themselves.
  // IMPORTANT: The score may simply be 0 for suggestions from providers which
  // cannot provide score values.
  float score() const { return score_; }
  void set_score(float score) { score_ = score; }

 private:
  ID id_;
  GURL url_;
  GURL amp_url_;
  base::string16 title_;
  base::string16 snippet_text_;
  base::Time publish_date_;
  base::string16 publisher_name_;
  float score_;

  DISALLOW_COPY_AND_ASSIGN(ContentSuggestion);
};

std::ostream& operator<<(std::ostream& os, ContentSuggestion::ID id);

}  // namespace ntp_snippets

#endif  // COMPONENTS_NTP_SNIPPETS_CONTENT_SUGGESTION_H_
