// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_METRICS_LEAK_DETECTOR_LEAK_DETECTOR_H_
#define COMPONENTS_METRICS_LEAK_DETECTOR_LEAK_DETECTOR_H_

#include <stddef.h>
#include <stdint.h>

#include <list>
#include <vector>

#include "base/feature_list.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_checker.h"

namespace base {
template <typename T>
struct DefaultLazyInstanceTraits;
}

namespace metrics {

namespace leak_detector {
class LeakDetectorImpl;
}

// LeakDetector is an interface layer that connects the allocator
// (base::allocator), the leak detector logic (LeakDetectorImpl), and any
// external classes interested in receiving leak reports (extend the Observer
// class).
//
// Only one instance of this class can exist. Access this instance using
// GetInstance(). Do not create an instance of this class directly.
//
// These member functions are thread-safe:
// - AllocHook
// - FreeHook
// - AddObserver
// - RemoveObserver
//
// All other functions must always be called from the same thread. This is
// enforced with a DCHECK.
class LeakDetector {
 public:
  // Contains a report of a detected memory leak.
  struct LeakReport {
    LeakReport();
    LeakReport(const LeakReport& other);
    ~LeakReport();

    size_t alloc_size_bytes;

    // Unlike the CallStack struct, which consists of addresses, this call stack
    // will contain offsets in the executable binary.
    std::vector<uintptr_t> call_stack;
  };

  // Interface for receiving leak reports.
  class Observer {
   public:
    virtual ~Observer() {}

    // Called by leak detector to report a leak.
    virtual void OnLeakFound(const LeakReport& report) = 0;
  };

  // Returns the sole instance, or creates it if it hasn't already been created.
  static LeakDetector* GetInstance();

  // Initializer arguments:
  // sampling_rate:
  //     Pseudorandomly sample a fraction of the incoming allocations and frees,
  //     based on hash values. Setting to 0 means no allocs/frees are sampled.
  //     Setting to 1.0 or more means all allocs/frees are sampled. Anything in
  //     between will result in an approximately that fraction of allocs/frees
  //     being sampled.
  // max_call_stack_unwind_depth:
  //     The max number of call stack frames to unwind.
  // analysis_interval_bytes:
  //     Perform a leak analysis each time this many bytes have been allocated
  //     since the previous analysis.
  // size_suspicion_threshold, call_stack_suspicion_threshold:
  //     A possible leak should be suspected this many times to take action on i
  //     For size analysis, the action is to start profiling by call stack.
  //     For call stack analysis, the action is to generate a leak report.
  void Init(float sampling_rate,
            size_t max_call_stack_unwind_depth,
            uint64_t analysis_interval_bytes,
            uint32_t size_suspicion_threshold,
            uint32_t call_stack_suspicion_threshold);

  // Add |observer| to the list of stored Observers, i.e. |observers_|, to which
  // the leak detector will report leaks.
  void AddObserver(Observer* observer);

  // Remove |observer| from |observers_|.
  void RemoveObserver(Observer* observer);

 private:
  friend base::DefaultLazyInstanceTraits<LeakDetector>;
  FRIEND_TEST_ALL_PREFIXES(LeakDetectorTest, NotifyObservers);

  // Keep these private, as this class is meant to be initialized only through
  // the lazy instance, and never destroyed.
  LeakDetector();
  ~LeakDetector();

  // Allocator hook function that processes each alloc. Performs sampling and
  // unwinds call stack if necessary. Passes the allocated memory |ptr| and
  // allocation size |size| along with call stack info to RecordAlloc().
  static void AllocHook(const void* ptr, size_t size);

  // Allocator hook function that processes each free. Performs sampling and
  // passes the allocation address |ptr| to |impl_|.
  static void FreeHook(const void* ptr);

  // Give an pointer |ptr|, computes a hash of the pointer value and compares it
  // against |sampling_factor_| to determine if it should be sampled. This
  // allows the same pointer to be sampled during both alloc and free.
  bool ShouldSample(const void* ptr) const;

  // Notifies all Observers in |observers_| with the given vector of leak
  // reports.
  void NotifyObservers(const std::vector<LeakReport>& reports);

  // List of observers to notify when there's a leak report.
  // TODO(sque): Consider using ObserverListThreadSafe instead.
  base::ObserverList<Observer> observers_;

  // For atomic access to |observers_|.
  base::Lock observers_lock_;

  // Handles leak detection logic. Must be called under lock as LeakDetectorImpl
  // uses shared resources.
  scoped_ptr<leak_detector::LeakDetectorImpl> impl_;

  // For thread safety.
  base::ThreadChecker thread_checker_;

  // Total number of bytes allocated, computed before sampling.
  size_t total_alloc_size_;

  // The value of |total_alloc_size_| the last time there was a leak analysis,
  // rounded down to the nearest multiple of |analysis_interval_bytes_|.
  size_t last_analysis_alloc_size_;

  // For atomic access to |impl_|, |total_alloc_size_| and
  // |last_analysis_alloc_size_|.
  base::Lock recording_lock_;

  // Perform a leak analysis each time this many bytes have been allocated since
  // the previous analysis.
  size_t analysis_interval_bytes_;

  // When unwinding call stacks, unwind no more than this number of frames.
  size_t max_call_stack_unwind_depth_;

  // Sampling factor used by ShouldSample(). It's full range of values
  // corresponds to the allowable range of |sampling_rate| passed in during
  // initialization: [0.0f, 1.0f] -> [0, UINT64_MAX].
  uint64_t sampling_factor_;

  DISALLOW_COPY_AND_ASSIGN(LeakDetector);
};

}  // namespace metrics

#endif  // COMPONENTS_METRICS_LEAK_DETECTOR_LEAK_DETECTOR_H_
