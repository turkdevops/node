// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LOCAL_HEAP_H_
#define V8_HEAP_LOCAL_HEAP_H_

#include <atomic>
#include <memory>

#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/execution/isolate.h"
#include "src/heap/concurrent-allocator.h"

namespace v8 {
namespace internal {

class Heap;
class Safepoint;
class LocalHandles;
class PersistentHandles;

class V8_EXPORT_PRIVATE LocalHeap {
 public:
  LocalHeap(Heap* heap,
            std::unique_ptr<PersistentHandles> persistent_handles = nullptr);
  ~LocalHeap();

  // Invoked by main thread to signal this thread that it needs to halt in a
  // safepoint.
  void RequestSafepoint();

  // Frequently invoked by local thread to check whether safepoint was requested
  // from the main thread.
  void Safepoint();

  LocalHandles* handles() { return handles_.get(); }

  template <typename T>
  inline Handle<T> NewPersistentHandle(T object);
  template <typename T>
  inline Handle<T> NewPersistentHandle(Handle<T> object);
  std::unique_ptr<PersistentHandles> DetachPersistentHandles();
#ifdef DEBUG
  bool ContainsPersistentHandle(Address* location);
  bool IsHandleDereferenceAllowed();
#endif

  bool IsParked();

  Heap* heap() { return heap_; }

  ConcurrentAllocator* old_space_allocator() { return &old_space_allocator_; }

  // Mark/Unmark linear allocation areas black. Used for black allocation.
  void MarkLinearAllocationAreaBlack();
  void UnmarkLinearAllocationArea();

  // Give up linear allocation areas. Used for mark-compact GC.
  void FreeLinearAllocationArea();

  // Create filler object in linear allocation areas. Verifying requires
  // iterable heap.
  void MakeLinearAllocationAreaIterable();

  // Fetches a pointer to the local heap from the thread local storage.
  // It is intended to be used in handle and write barrier code where it is
  // difficult to get a pointer to the current instance of local heap otherwise.
  // The result may be a nullptr if there is no local heap instance associated
  // with the current thread.
  static LocalHeap* Current();

 private:
  enum class ThreadState {
    // Threads in this state need to be stopped in a safepoint.
    Running,
    // Thread was parked, which means that the thread is not allowed to access
    // or manipulate the heap in any way.
    Parked,
    // Thread was stopped in a safepoint.
    Safepoint
  };

  void Park();
  void Unpark();
  void EnsureParkedBeforeDestruction();

  void EnsurePersistentHandles();

  bool IsSafepointRequested();
  void ClearSafepointRequested();

  void EnterSafepoint();

  Heap* heap_;

  base::Mutex state_mutex_;
  base::ConditionVariable state_change_;
  ThreadState state_;

  std::atomic<bool> safepoint_requested_;

  bool allocation_failed_;

  LocalHeap* prev_;
  LocalHeap* next_;

  std::unique_ptr<LocalHandles> handles_;
  std::unique_ptr<PersistentHandles> persistent_handles_;

  ConcurrentAllocator old_space_allocator_;

  friend class Heap;
  friend class GlobalSafepoint;
  friend class ParkedScope;
  friend class ConcurrentAllocator;
};

class ParkedScope {
 public:
  explicit ParkedScope(LocalHeap* local_heap) : local_heap_(local_heap) {
    local_heap_->Park();
  }

  ~ParkedScope() { local_heap_->Unpark(); }

 private:
  LocalHeap* local_heap_;
};

class ParkedMutexGuard {
  base::Mutex* guard_;

 public:
  explicit ParkedMutexGuard(LocalHeap* local_heap, base::Mutex* guard)
      : guard_(guard) {
    ParkedScope scope(local_heap);
    guard_->Lock();
  }

  ~ParkedMutexGuard() { guard_->Unlock(); }
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_LOCAL_HEAP_H_
