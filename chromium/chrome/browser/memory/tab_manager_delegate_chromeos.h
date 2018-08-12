// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEMORY_TAB_MANAGER_DELEGATE_CHROMEOS_H_
#define CHROME_BROWSER_MEMORY_TAB_MANAGER_DELEGATE_CHROMEOS_H_

#include <utility>
#include <vector>

#include "base/containers/hash_tables.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/timer/timer.h"
#include "chrome/browser/chromeos/arc/arc_process.h"
#include "chrome/browser/memory/tab_manager.h"
#include "chrome/browser/memory/tab_stats.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

namespace memory {

// The importance of tabs/apps. The lower the value, the more likely a process
// is to be selected on memory pressure. The values is additive, for example,
// one tab could be of value (CHROME_NORMAL | CHROME_PINNED).
// TODO(cylee): Refactor this CL so the prioritize logic is unified in
// TabManager.
enum ProcessPriority {
  ANDROID_BACKGROUND = 1,
  ANDROID_SERVICE = 1 << 1,
  ANDROID_CANT_SAVE_STATE = 1 << 2,
  ANDROID_PERCEPTIBLE = 1 << 3,
  ANDROID_VISIBLE = 1 << 4,
  ANDROID_TOP_SLEEPING = 1 << 5,
  ANDROID_FOREGROUND_SERVICE = 1 << 6,
  ANDROID_FOREGROUND = 1 << 7,
  // A chrome window can be of one of the 3 exclusive types below:
  // internal page, normal page, or chrome app.
  CHROME_INTERNAL = 1 << 8,
  CHROME_NORMAL = 1 << 9,
  CHROME_APP = 1 << 10,
  // A chrome window could have the following 4 additional attributes
  // (not exclusive).
  CHROME_PINNED = 1 << 11,
  CHROME_MEDIA = 1 << 12,
  CHROME_CANT_SAVE_STATE = 1 << 13,
  CHROME_SELECTED = 1 << 14,
  ANDROID_TOP = CHROME_SELECTED,

  ANDROID_PERSISTENT = 1 << 30,
  ANDROID_NON_EXISTS = 0x7FFFFFFF
};

// The Chrome OS TabManagerDelegate is responsible for keeping the
// renderers' scores up to date in /proc/<pid>/oom_score_adj.
//
// Note that AdjustOomPriorities will be called on the UI thread by
// TabManager, but the actual work will take place on the file thread
// (see implementation of AdjustOomPriorities).
class TabManagerDelegate : public content::NotificationObserver {
 public:
  TabManagerDelegate();
  ~TabManagerDelegate() override;

  void LowMemoryKill(const base::WeakPtr<TabManager>& tab_manager,
                     const TabStatsList& tab_stats);

  // Return the score of a process.
  int GetOomScore(int child_process_host_id);

  // Called when the timer fires, sets oom_adjust_score for all renderers.
  void AdjustOomPriorities(const TabStatsList& stats_list);

 private:
  FRIEND_TEST_ALL_PREFIXES(TabManagerDelegateTest, GetProcessHandles);
  FRIEND_TEST_ALL_PREFIXES(TabManagerDelegateTest, LowMemoryKill);

  // On ARC enabled machines, either a tab or an app could be a possible
  // victim of low memory kill process. This is a helper struct which holds a
  // pointer to an app or a tab (but not both) to facilitate prioritizing the
  // victims.
  struct KillCandidate {
    KillCandidate(const TabStats* _tab, int _priority)
        : tab(_tab), priority(_priority), is_arc_app(false) {}
    KillCandidate(const arc::ArcProcess* _app, int _priority)
        : app(_app), priority(_priority), is_arc_app(true) {}
    union {
      const TabStats* tab;
      const arc::ArcProcess* app;
    };
    int priority;
    bool is_arc_app;

    bool operator<(const KillCandidate& rhs) const {
      return priority < rhs.priority;
    }
  };

  // content::NotificationObserver:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

  // Pair to hold child process host id and ProcessHandle.
  typedef std::pair<int, base::ProcessHandle> ProcessInfo;

  // Returns a list of child process host ids and ProcessHandles from
  // |stats_list| with unique pids. If multiple tabs use the same process,
  // returns the first child process host id and corresponding pid. This implies
  // that the processes are selected based on their "most important" tab.
  static std::vector<ProcessInfo> GetChildProcessInfos(
      const TabStatsList& stats_list);

  // Actual kills a process after getting all info of tabs and apps.
  static void LowMemoryKillImpl(
      const base::WeakPtr<TabManager>& tab_manager,
      const TabStatsList& tab_list,
      const std::vector<arc::ArcProcess>& arc_processes);

  // Get the list of candidates to kill, sorted by reversed importance.
  static std::vector<KillCandidate> GetSortedKillCandidates(
      const TabStatsList& tab_list,
      const std::vector<arc::ArcProcess>& arc_processes);

  // Called by AdjustOomPriorities.
  void AdjustOomPrioritiesOnFileThread(TabStatsList stats_list);

  // Posts AdjustFocusedTabScore task to the file thread.
  void OnFocusTabScoreAdjustmentTimeout();

  // Sets the score of the focused tab to the least value.
  void AdjustFocusedTabScoreOnFileThread();

  // Registrar to receive renderer notifications.
  content::NotificationRegistrar registrar_;
  // Timer to guarantee that the tab is focused for a certain amount of time.
  base::OneShotTimer focus_tab_score_adjust_timer_;
  // This lock is for |oom_score_map_| and |focused_tab_process_info_|.
  base::Lock oom_score_lock_;
  // Map maintaining the child process host id - oom_score mapping.
  typedef base::hash_map<int, int> ProcessScoreMap;
  ProcessScoreMap oom_score_map_;
  // Holds the focused tab's child process host id.
  ProcessInfo focused_tab_process_info_;

  DISALLOW_COPY_AND_ASSIGN(TabManagerDelegate);
};

}  // namespace memory

#endif  // CHROME_BROWSER_MEMORY_TAB_MANAGER_DELEGATE_CHROMEOS_H_
