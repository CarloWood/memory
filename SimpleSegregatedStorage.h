/**
 * memory -- C++ Memory utilities
 *
 * @file
 * @brief Declaration of class SimpleSegregatedStorage.
 *
 * @Copyright (C) 2019 - 2025  Carlo Wood.
 *
 * pub   dsa3072/C155A4EEE4E527A2 2018-08-16 Carlo Wood (CarloWood on Libera) <carlo@alinoe.com>
 * fingerprint: 8020 B266 6305 EE2F D53E  6827 C155 A4EE E4E5 27A2
 *
 * This file is part of memory.
 *
 * memory is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * memory is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with memory.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "PtrTag.h"
#include "utils/macros.h"
#include <atomic>
#include <functional>
#include <mutex>

namespace memory {

// Consistent state of SimpleSegregatedStorage exists of a singly linked list of FreeNode's.
//
//  head_ -->.-------------.   .-->.-------------.   .-->.-------------.   .-->.-------------.
//           | next_ ------+--'    | next_ ------+--'    | next_ ------+--'    | next_ ------+--> nullptr
//           |             |       |             |       |             |       |             |
//           `-------------'       `-------------'       `-------------'       `-------------'
//
// Calling allocate() must return one block, removing it from the free list.
// In the single threaded case that will be the first block, so that the result
// after calling allocate() is:
//
//  head_ ----------------------.
//  ptr ---->.-------------.   .-'>.-------------.   .-->.-------------.   .-->.-------------.
//           | next_ ------+--'    | next_ ------+--'    | next_ ------+--'    | next_ ------+--> nullptr
//           |             |       |             |       |             |       |             |
//           `-------------'       `-------------'       `-------------'       `-------------'
//
// Or in code:
//
//   node = head_
//   head_ = node->next_;
//   return node;
//
// When multiple threads can call allocate() concurrently, this can only be implemented
// in a lock-free way by using an atomic compare and exchange operation.
//
// Deallocating a node is the other way around:
//
//   node->next_ = head_;
//   head_ = node;

// SimpleSegregatedStorageBase
//
// Maintains an unordered free list of blocks.
//
class SimpleSegregatedStorageBase
{
 protected:
  std::atomic<std::uintptr_t> head_tag_;        // Encodes a pointer that points to the first free memory block in the free-list,
                                                // or end_of_list if the free-list is empty. Also encodes a "tag" of a few bits.

  // Construct an empty free list.
  SimpleSegregatedStorageBase() : head_tag_(PtrTag::end_of_list) { }

  // Allow pointers to SimpleSegregatedStorageBase that are allocated on the heap I guess...
  virtual ~SimpleSegregatedStorageBase() = default;

  // Called if `allocate()` runs into the end of the list.
  // Returning false means that this storage is simply out of memory.
  virtual bool try_allocate_more(std::function<bool()> const& add_new_block) { return false; }

  [[gnu::always_inline]] bool CAS_head_tag(PtrTag& head_tag, PtrTag new_head_tag, std::memory_order order)
  {
    return head_tag_.compare_exchange_weak(head_tag.encoded_, new_head_tag.encoded_, order);
  }

 public:
  // Initialize this SimpleSegregatedStorage with an existing free-list.
  void initialize(void* head)
  {
    // Call this after default construction, before using the segregated storage.
    ASSERT(head_tag_ == PtrTag::end_of_list);
    head_tag_ = PtrTag::encode(head, 0);
  }

  void* allocate(std::function<bool()> const& add_new_block)
  {
    for (;;)
    {
      // Load the current value of head_tag_ into `head_tag`.
      // Use std::memory_order_acquire to synchronize with the std::memory_order_release in deallocate,
      // so that value of `next` read below will be the value written in deallocate corresponding to
      // this head value.
      PtrTag head_tag(head_tag_.load(std::memory_order_acquire));
      while (head_tag != PtrTag::end_of_list)
      {
        PtrTag new_head_tag = head_tag.next();
        // The std::memory_order_acquire is used in case of failure and required for the next
        // read of next_ at the top of the current loop (the previous line).
        if (AI_LIKELY(CAS_head_tag(head_tag, new_head_tag, std::memory_order_acquire)))
          // Return the old head.
          return head_tag.ptr();
        // head_tag_ was changed (the new value is now in `head_tag`). Try again with the new value.
      }
      // Reached the end of the list, try to allocate more memory.
      if (!try_allocate_more(add_new_block))
        return nullptr;
    }
  }

  // ptr must be a value previously returned by allocate().
  void deallocate(void* ptr)
  {
    typename PtrTag::FreeNode* const new_front_node = static_cast<typename PtrTag::FreeNode*>(ptr);
    PtrTag head_tag(head_tag_.load(std::memory_order_relaxed));
    for (;;)
    {
      PtrTag const new_head_tag(new_front_node, head_tag.tag());
      new_front_node->next_ = head_tag.ptr();
      // The std::memory_order_release is used in the case of success and causes the above
      // store to `new_front_node->next_` to be visible after a load-acquire of head_tag_
      // in allocate that reads the value of this `new_head_tag`.
      if (AI_LIKELY(CAS_head_tag(head_tag, new_head_tag, std::memory_order_release)))
        return;
    }
  }
};

class SimpleSegregatedStorage : public SimpleSegregatedStorageBase
{
 public:                                // To be used with std::scoped_lock<std::mutex> from calling classes.
  std::mutex add_block_mutex_;          // Protect against calling add_block concurrently.

 public:
  using SimpleSegregatedStorageBase::SimpleSegregatedStorageBase;

  bool try_allocate_more(std::function<bool()> const& add_new_block) override;
  void add_block(void* block, size_t block_size, size_t partition_size);
};

} // namespace memory
