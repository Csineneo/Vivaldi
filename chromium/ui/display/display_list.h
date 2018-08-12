// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_DISPLAY_DISPLAY_LIST_H_
#define UI_DISPLAY_DISPLAY_LIST_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include "base/observer_list.h"
#include "ui/display/display.h"
#include "ui/display/display_export.h"

namespace display {

class Display;
class DisplayList;
class DisplayObserver;

// See description in DisplayLock::SuspendObserverUpdates.
class DISPLAY_EXPORT DisplayListObserverLock {
 public:
  ~DisplayListObserverLock();

 private:
  friend class DisplayList;

  explicit DisplayListObserverLock(DisplayList* display_list);

  DisplayList* display_list_;

  DISALLOW_COPY_AND_ASSIGN(DisplayListObserverLock);
};

// Maintains an ordered list of display::Displays as well as operations to add,
// remove and update said list. Additionally maintains display::DisplayObservers
// and updates them as appropriate.
class DISPLAY_EXPORT DisplayList {
 public:
  using Displays = std::vector<display::Display>;

  enum class Type {
    PRIMARY,
    NOT_PRIMARY,
  };

  DisplayList();
  ~DisplayList();

  void AddObserver(display::DisplayObserver* observer);
  void RemoveObserver(display::DisplayObserver* observer);

  const Displays& displays() const { return displays_; }

  Displays::const_iterator FindDisplayById(int64_t id) const;
  Displays::iterator FindDisplayById(int64_t id);

  Displays::const_iterator GetPrimaryDisplayIterator() const;

  // Internally increments a counter that while non-zero results in observers
  // not being called for any changes to the displays. It is assumed once
  // callers release the last lock they call the observers appropriately.
  std::unique_ptr<DisplayListObserverLock> SuspendObserverUpdates();

  // Updates the cached id based on display.id() as well as whether the Display
  // is the primary display.
  void UpdateDisplay(const display::Display& display, Type type);

  // Adds a new Display.
  void AddDisplay(const display::Display& display, Type type);

  // Removes the Display with the specified id.
  void RemoveDisplay(int64_t id);

  base::ObserverList<display::DisplayObserver>* observers() {
    return &observers_;
  }

 private:
  friend class DisplayListObserverLock;

  bool should_notify_observers() const {
    return observer_suspend_lock_count_ == 0;
  }
  void IncrementObserverSuspendLockCount();
  void DecrementObserverSuspendLockCount();

  std::vector<display::Display> displays_;
  int primary_display_index_ = -1;
  base::ObserverList<display::DisplayObserver> observers_;

  int observer_suspend_lock_count_ = 0;

  DISALLOW_COPY_AND_ASSIGN(DisplayList);
};

}  // namespace display

#endif  // UI_DISPLAY_DISPLAY_LIST_H_
