// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/declarative_content/chrome_content_rules_registry.h"

#include <utility>

#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/declarative_content/content_action.h"
#include "chrome/browser/extensions/api/declarative_content/content_condition.h"
#include "chrome/browser/extensions/api/declarative_content/content_constants.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/api/declarative/rules_registry_service.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"

using url_matcher::URLMatcherConditionSet;

namespace extensions {

namespace {

// Factory method that creates DeclarativeContentConditions for |extension|
// according to the JSON array |condition_values| passed by the extension
// API. Sets |error| in case of an error.
void CreateConditions(
    const Extension* extension,
    url_matcher::URLMatcherConditionFactory* url_matcher_condition_factory,
    const std::vector<linked_ptr<base::Value>>& condition_values,
    DeclarativeContentConditions* conditions,
    std::string* error) {
  DeclarativeContentConditions created_conditions;

  for (const linked_ptr<base::Value>& value : condition_values) {
    scoped_ptr<ContentCondition> condition = ContentCondition::Create(
        extension, url_matcher_condition_factory, *value, error);
    if (!error->empty())
      return;
    created_conditions.push_back(make_linked_ptr(condition.release()));
  }

  created_conditions.swap(*conditions);
}

// Factory method that creates DeclarativeContentActions for |extension|
// according to |actions| which represents the array of actions received from
// the extension API.
void CreateActions(content::BrowserContext* browser_context,
                   const Extension* extension,
                   const std::vector<linked_ptr<base::Value>>& action_values,
                   DeclarativeContentActions* actions,
                   std::string* error) {
  DeclarativeContentActions created_actions;

  for (const linked_ptr<base::Value>& value : action_values) {
    scoped_refptr<const ContentAction> action =
        ContentAction::Create(browser_context, extension, *value, error);
    if (!error->empty())
      return;
    created_actions.push_back(action);
  }

  created_actions.swap(*actions);
}

// Creates a DeclarativeContentRule for |extension| given a json definition.
// The format of each condition and action's json is up to the specific
// ContentCondition and ContentAction.  |extension| may be NULL in tests.
//
// If |error| is empty, the translation was successful and the returned rule is
// internally consistent.
scoped_ptr<DeclarativeContentRule> CreateRule(
    url_matcher::URLMatcherConditionFactory* url_matcher_condition_factory,
    content::BrowserContext* browser_context,
    const Extension* extension,
    const core_api::events::Rule& api_rule,
    std::string* error) {
  scoped_ptr<DeclarativeContentRule> rule(new DeclarativeContentRule);

  rule->extension = extension;

  CreateConditions(extension, url_matcher_condition_factory,
                   api_rule.conditions, &rule->conditions, error);
  if (!error->empty())
    return scoped_ptr<DeclarativeContentRule>();

  CreateActions(browser_context, extension, api_rule.actions, &rule->actions,
                error);
  if (!error->empty())
    return scoped_ptr<DeclarativeContentRule>();

  rule->priority = *api_rule.priority;

  // Note: |api_rule| may contain tags, but these are ignored.

  return rule;
}

}  // namespace

//
// EvaluationScope
//

// Used to coalesce multiple requests for evaluation into a zero or one actual
// evaluations (depending on the EvaluationDisposition).  This is required for
// correctness when multiple trackers respond to the same event. Otherwise,
// executing the request from the first tracker will be done before the tracked
// state has been updated for the other trackers.
class ChromeContentRulesRegistry::EvaluationScope {
 public:
  // Default disposition is PERFORM_EVALUATION.
  explicit EvaluationScope(ChromeContentRulesRegistry* registry);
  EvaluationScope(ChromeContentRulesRegistry* registry,
                  EvaluationDisposition disposition);
  ~EvaluationScope();

 private:
  ChromeContentRulesRegistry* const registry_;
  const EvaluationDisposition previous_disposition_;

  DISALLOW_COPY_AND_ASSIGN(EvaluationScope);
};

ChromeContentRulesRegistry::EvaluationScope::EvaluationScope(
    ChromeContentRulesRegistry* registry)
    : EvaluationScope(registry, DEFER_REQUESTS) {}

ChromeContentRulesRegistry::EvaluationScope::EvaluationScope(
    ChromeContentRulesRegistry* registry,
    EvaluationDisposition disposition)
    : registry_(registry),
      previous_disposition_(registry_->evaluation_disposition_) {
  DCHECK_NE(EVALUATE_REQUESTS, disposition);
  registry_->evaluation_disposition_ = disposition;
}

ChromeContentRulesRegistry::EvaluationScope::~EvaluationScope() {
  registry_->evaluation_disposition_ = previous_disposition_;
  if (registry_->evaluation_disposition_ == EVALUATE_REQUESTS) {
    for (content::WebContents* tab : registry_->evaluation_pending_)
      registry_->EvaluateConditionsForTab(tab);
    registry_->evaluation_pending_.clear();
  }
}

//
// ChromeContentRulesRegistry
//

ChromeContentRulesRegistry::ChromeContentRulesRegistry(
    content::BrowserContext* browser_context,
    RulesCacheDelegate* cache_delegate)
    : ContentRulesRegistry(browser_context,
                           declarative_content_constants::kOnPageChanged,
                           content::BrowserThread::UI,
                           cache_delegate,
                           RulesRegistryService::kDefaultRulesRegistryID),
      page_url_condition_tracker_(browser_context, this),
      css_condition_tracker_(browser_context, this),
      is_bookmarked_condition_tracker_(browser_context, this),
      evaluation_disposition_(EVALUATE_REQUESTS) {
  registrar_.Add(this,
                 content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
                 content::NotificationService::AllBrowserContextsAndSources());
}

void ChromeContentRulesRegistry::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  switch (type) {
    case content::NOTIFICATION_WEB_CONTENTS_DESTROYED: {
      content::WebContents* tab =
          content::Source<content::WebContents>(source).ptr();
      // Note that neither non-tab WebContents nor tabs from other browser
      // contexts will be in the map.
      active_rules_.erase(tab);
      break;
    }
  }
}

void ChromeContentRulesRegistry::RequestEvaluation(
    content::WebContents* contents) {
  switch (evaluation_disposition_) {
    case EVALUATE_REQUESTS:
      EvaluateConditionsForTab(contents);
      break;
    case DEFER_REQUESTS:
      evaluation_pending_.insert(contents);
      break;
    case IGNORE_REQUESTS:
      break;
  }
}

bool ChromeContentRulesRegistry::ShouldManageConditionsForBrowserContext(
    content::BrowserContext* context) {
  return ManagingRulesForBrowserContext(context);
}

void ChromeContentRulesRegistry::MonitorWebContentsForRuleEvaluation(
    content::WebContents* contents) {
  // We rely on active_rules_ to have a key-value pair for |contents| to know
  // which WebContents we are working with.
  active_rules_[contents] = std::set<const DeclarativeContentRule*>();

  EvaluationScope evaluation_scope(this);
  page_url_condition_tracker_.TrackForWebContents(contents);
  css_condition_tracker_.TrackForWebContents(contents);
  is_bookmarked_condition_tracker_.TrackForWebContents(contents);
}

void ChromeContentRulesRegistry::DidNavigateMainFrame(
    content::WebContents* contents,
    const content::LoadCommittedDetails& details,
    const content::FrameNavigateParams& params) {
  if (ContainsKey(active_rules_, contents)) {
    EvaluationScope evaluation_scope(this);
    page_url_condition_tracker_.OnWebContentsNavigation(contents, details,
                                                        params);
    css_condition_tracker_.OnWebContentsNavigation(contents, details, params);
    is_bookmarked_condition_tracker_.OnWebContentsNavigation(contents, details,
                                                             params);
  }
}

bool ChromeContentRulesRegistry::ManagingRulesForBrowserContext(
    content::BrowserContext* context) {
  // Manage both the normal context and incognito contexts associated with it.
  return Profile::FromBrowserContext(context)->GetOriginalProfile() ==
      Profile::FromBrowserContext(browser_context());
}

std::set<const DeclarativeContentRule*> ChromeContentRulesRegistry::GetMatches(
    const RendererContentMatchData& renderer_data,
    bool is_incognito_renderer) const {
  std::set<const DeclarativeContentRule*> matching_rules;

  // First get the (rule, condition) pairs that have URL matches. Then for
  // those, evaluate whether all the sub-conditions are fulfilled. Since a rule
  // matches if *any* of its conditions match, immediately record the rule
  // as matching if all the sub-conditions are fulfilled.
  for (URLMatcherConditionSet::ID url_match : renderer_data.page_url_matches) {
    RuleAndConditionForURLMatcherId::const_iterator rule_condition_iter =
        rule_and_conditions_for_match_id_.find(url_match);
    CHECK(rule_condition_iter != rule_and_conditions_for_match_id_.end());

    const std::pair<const DeclarativeContentRule*, const ContentCondition*>&
        rule_condition_pair = rule_condition_iter->second;
    const DeclarativeContentRule* rule = rule_condition_pair.first;
    if (is_incognito_renderer) {
      if (!util::IsIncognitoEnabled(rule->extension->id(), browser_context()))
        continue;

      // Split-mode incognito extensions register their rules with separate
      // RulesRegistries per Original/OffTheRecord browser contexts, whereas
      // spanning-mode extensions share the Original browser context.
      if (util::CanCrossIncognito(rule->extension, browser_context())) {
        // The extension uses spanning mode incognito. No rules should have been
        // registered for the extension in the OffTheRecord registry so
        // execution for that registry should never reach this point.
        CHECK(!browser_context()->IsOffTheRecord());
      } else {
        // The extension uses split mode incognito. Both the Original and
        // OffTheRecord registries may have (separate) rules for this extension.
        // We've established above that we are looking at an incognito renderer,
        // so only the OffTheRecord registry should process its rules.
        if (!browser_context()->IsOffTheRecord())
          continue;
      }
    }

    const ContentCondition* condition = rule_condition_pair.second;
    if (condition->IsFulfilled(renderer_data))
      matching_rules.insert(rule);
  }
  return matching_rules;
}

std::string ChromeContentRulesRegistry::AddRulesImpl(
    const std::string& extension_id,
    const std::vector<linked_ptr<RulesRegistry::Rule> >& rules) {
  EvaluationScope evaluation_scope(this);
  const Extension* extension = ExtensionRegistry::Get(browser_context())
      ->GetInstalledExtension(extension_id);
  DCHECK(extension) << "Must have extension with id " << extension_id;

  std::string error;
  RulesMap new_content_rules;

  for (const linked_ptr<RulesRegistry::Rule>& rule : rules) {
    ExtensionRuleIdPair rule_id(extension, *rule->id);
    DCHECK(content_rules_.find(rule_id) == content_rules_.end());

    scoped_ptr<DeclarativeContentRule> content_rule(CreateRule(
        page_url_condition_tracker_.condition_factory(),
        browser_context(),
        extension,
        *rule,
        &error));
    if (!error.empty()) {
      // Clean up temporary condition sets created during rule creation.
      page_url_condition_tracker_.ClearUnusedConditionSets();
      return error;
    }
    DCHECK(content_rule);

    new_content_rules[rule_id] = make_linked_ptr(content_rule.release());
  }

  // Wohoo, everything worked fine.
  content_rules_.insert(new_content_rules.begin(), new_content_rules.end());

  // Create the triggers.
  for (const RulesMap::value_type& rule_id_rule_pair : new_content_rules) {
    const linked_ptr<const DeclarativeContentRule>& rule =
        rule_id_rule_pair.second;
    for (const linked_ptr<const ContentCondition>& condition :
             rule->conditions) {
      URLMatcherConditionSet::Vector url_condition_sets;
      condition->GetURLMatcherConditionSets(&url_condition_sets);
      for (const scoped_refptr<URLMatcherConditionSet>& condition_set :
               url_condition_sets) {
        rule_and_conditions_for_match_id_[condition_set->id()] =
            std::make_pair(rule.get(), condition.get());
      }
    }
  }

  // Register url patterns in the URL matcher.
  URLMatcherConditionSet::Vector all_new_condition_sets;
  for (const RulesMap::value_type& rule_id_rule_pair : new_content_rules) {
    const linked_ptr<const DeclarativeContentRule>& rule =
        rule_id_rule_pair.second;
    for (const linked_ptr<const ContentCondition>& condition :
             rule->conditions) {
      condition->GetURLMatcherConditionSets(&all_new_condition_sets);
    }
  }
  page_url_condition_tracker_.AddConditionSets(
      all_new_condition_sets);

  UpdateCssSelectorsFromRules();

  return std::string();
}

std::string ChromeContentRulesRegistry::RemoveRulesImpl(
    const std::string& extension_id,
    const std::vector<std::string>& rule_identifiers) {
  // Ignore evaluation requests in this function because it reverts actions on
  // any active rules itself. Otherwise, we run the risk of reverting the same
  // rule multiple times.
  EvaluationScope evaluation_scope(this, IGNORE_REQUESTS);
  // URLMatcherConditionSet IDs that can be removed from URLMatcher.
  std::vector<URLMatcherConditionSet::ID> condition_set_ids_to_remove;

  const Extension* extension = ExtensionRegistry::Get(browser_context())
      ->GetInstalledExtension(extension_id);
  for (const std::string& id : rule_identifiers) {
    // Skip unknown rules.
    RulesMap::iterator content_rules_entry =
        content_rules_.find(std::make_pair(extension, id));
    if (content_rules_entry == content_rules_.end())
      continue;

    // Remove all triggers but collect their IDs.
    URLMatcherConditionSet::Vector condition_sets;
    const DeclarativeContentRule* rule = content_rules_entry->second.get();
    for (const linked_ptr<const ContentCondition>& condition :
             rule->conditions) {
      condition->GetURLMatcherConditionSets(&condition_sets);
    }
    for (const scoped_refptr<URLMatcherConditionSet>& condition_set :
         condition_sets) {
      condition_set_ids_to_remove.push_back(condition_set->id());
      rule_and_conditions_for_match_id_.erase(condition_set->id());
    }

    // Remove the DeclarativeContentRule from active_rules_.
    for (auto& tab_rules_pair : active_rules_) {
      if (ContainsKey(tab_rules_pair.second, rule)) {
        ContentAction::ApplyInfo apply_info =
            {rule->extension, browser_context(), tab_rules_pair.first,
             rule->priority};
        for (const scoped_refptr<const ContentAction>& action : rule->actions)
          action->Revert(apply_info);
        tab_rules_pair.second.erase(rule);
      }
    }

    // Remove reference to actual rule.
    content_rules_.erase(content_rules_entry);
  }

  // Clear URLMatcher of condition sets that are not needed any more.
  page_url_condition_tracker_.RemoveConditionSets(
      condition_set_ids_to_remove);

  UpdateCssSelectorsFromRules();

  return std::string();
}

std::string ChromeContentRulesRegistry::RemoveAllRulesImpl(
    const std::string& extension_id) {
  // Search all identifiers of rules that belong to extension |extension_id|.
  std::vector<std::string> rule_identifiers;
  for (const RulesMap::value_type& id_rule_pair : content_rules_) {
    const ExtensionRuleIdPair& extension_rule_id_pair = id_rule_pair.first;
    if (extension_rule_id_pair.first->id() == extension_id)
      rule_identifiers.push_back(extension_rule_id_pair.second);
  }

  return RemoveRulesImpl(extension_id, rule_identifiers);
}

void ChromeContentRulesRegistry::UpdateCssSelectorsFromRules() {
  std::set<std::string> css_selectors;  // We rely on this being sorted.
  for (const RulesMap::value_type& id_rule_pair : content_rules_) {
    const DeclarativeContentRule* rule = id_rule_pair.second.get();
    for (const linked_ptr<const ContentCondition>& condition :
             rule->conditions) {
      const std::vector<std::string>& condition_css_selectors =
          condition->css_selectors();
      css_selectors.insert(condition_css_selectors.begin(),
                           condition_css_selectors.end());
    }
  }

  css_condition_tracker_.SetWatchedCssSelectors(css_selectors);
}

void ChromeContentRulesRegistry::EvaluateConditionsForTab(
    content::WebContents* tab) {
  extensions::RendererContentMatchData renderer_data;
  page_url_condition_tracker_.GetMatches(tab, &renderer_data.page_url_matches);
  css_condition_tracker_.GetMatchingCssSelectors(tab,
                                                 &renderer_data.css_selectors);
  renderer_data.is_bookmarked =
      is_bookmarked_condition_tracker_.IsUrlBookmarked(tab);
  std::set<const DeclarativeContentRule*> matching_rules =
      GetMatches(renderer_data, tab->GetBrowserContext()->IsOffTheRecord());
  if (matching_rules.empty() && !ContainsKey(active_rules_, tab))
    return;

  std::set<const DeclarativeContentRule*>& prev_matching_rules =
      active_rules_[tab];
  for (const DeclarativeContentRule* rule : matching_rules) {
    ContentAction::ApplyInfo apply_info =
        {rule->extension, browser_context(), tab, rule->priority};
    if (!ContainsKey(prev_matching_rules, rule)) {
      for (const scoped_refptr<const ContentAction>& action : rule->actions)
        action->Apply(apply_info);
    } else {
      for (const scoped_refptr<const ContentAction>& action : rule->actions)
        action->Reapply(apply_info);
    }
  }
  for (const DeclarativeContentRule* rule : prev_matching_rules) {
    if (!ContainsKey(matching_rules, rule)) {
      ContentAction::ApplyInfo apply_info =
          {rule->extension, browser_context(), tab, rule->priority};
      for (const scoped_refptr<const ContentAction>& action : rule->actions)
        action->Revert(apply_info);
    }
  }

  if (matching_rules.empty())
    active_rules_[tab].clear();
  else
    swap(matching_rules, prev_matching_rules);
}

bool ChromeContentRulesRegistry::IsEmpty() const {
  return rule_and_conditions_for_match_id_.empty() && content_rules_.empty() &&
         page_url_condition_tracker_.IsEmpty();
}

void ChromeContentRulesRegistry::UpdateMatchingCssSelectorsForTesting(
    content::WebContents* contents,
    const std::vector<std::string>& matching_css_selectors) {
  css_condition_tracker_.UpdateMatchingCssSelectorsForTesting(
      contents,
      matching_css_selectors);
}

size_t ChromeContentRulesRegistry::GetActiveRulesCountForTesting() {
  size_t count = 0;
  for (auto web_contents_rules_pair : active_rules_)
    count += web_contents_rules_pair.second.size();
  return count;
}

ChromeContentRulesRegistry::~ChromeContentRulesRegistry() {
}

}  // namespace extensions
