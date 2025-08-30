/**
 * memory -- C++ Memory utilities
 *
 * @file
 * @brief Definition of class SimpleSegregatedStorage.
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

#include "utils/macros.h"
#include <atomic>
#include <functional>
#include <mutex>

namespace memory {

// Consistent state of SimpleSegregatedStorage exists of a singly linked list of FreeNode's.
//
//  m_head -->.--------------.   .-->.--------------.   .-->.--------------.   .-->.--------------.
//             | m_next ------+--'    | m_next ------+--'    | m_next ------+--'    | m_next ------+--> nullptr
//             |              |       |              |       |              |       |              |
//             `--------------'       `--------------'       `--------------'       `--------------'
//
// Calling allocate() must return one block, removing it from the free list.
// In the single threaded case that will be the first block, so that the result
// after calling allocate() is:
//
//  m_head -----------------------.
//  ptr ------>.--------------.   .-'>.--------------.   .-->.--------------.   .-->.--------------.
//             | m_next ------+--'    | m_next ------+--'    | m_next ------+--'    | m_next ------+--> nullptr
//             |              |       |              |       |              |       |              |
//             `--------------'       `--------------'       `--------------'       `--------------'
//
// Or in code:
//
// node = m_head
// m_head = node->m_next;
// return node;
//
// When multiple threads can call allocate() concurrently, this can only be implemented
// in a lock-free way by using an atomic compare and exchange operation.
//
// Deallocating a node is the other way around:
//
// node->m_next = m_head;
// m_head = node;

// SimpleSegregatedStorageBase
//
// Maintains an unordered free list of blocks.
//
class SimpleSegregatedStorageBase
{
 protected:
  struct FreeNode { FreeNode* m_next; };

  std::atomic<FreeNode*> m_head;        // Points to the first free memory block in the free-list, or nullptr if the free-list is empty.

  // Construct an empty free list.
  SimpleSegregatedStorageBase() : m_head(nullptr) { }
  // Allow pointers to SimpleSegregatedStorageBase that are allocated on the heap I guess...
  virtual ~SimpleSegregatedStorageBase() = default;

  // Called if `allocate()` runs into the end of the list (m_head becomes null).
  // Returning false means that this storage is simply out of memory.
  virtual bool try_allocate_more(std::function<bool()> const& add_new_block) { return false; }

 public:
  void* allocate(std::function<bool()> const& add_new_block)
  {
    // Load the current value of m_head into `head`.
    // Use std::memory_order_acquire to synchronize with the std::memory_order_release in deallocate,
    // so that value of `next` read below will be the value written in deallocate corresponding to
    // this head value.
    for (;;)
    {
      FreeNode* head = m_head.load(std::memory_order_acquire);
      while (head)
      {
        FreeNode* next = head->m_next;
        // The std::memory_order_acquire is used in case of failure and required for the next
        // read of m_next at the top of the current loop (the previous line).
        if (AI_LIKELY(m_head.compare_exchange_weak(head, next, std::memory_order_acquire)))
          // Return the previous value of m_head (that we just replaced with `next`).
          return head;
        // m_head was changed (this value is now in `head`). Try again with the new value.
      }
      // Reached the end of the list, try to allocate more memory.
      if (!try_allocate_more(add_new_block))
        return nullptr;
    }
  }

  // ptr must be a value previously returned by allocate().
  void deallocate(void* ptr)
  {
    FreeNode* node = static_cast<FreeNode*>(ptr);
    node->m_next = m_head.load(std::memory_order_relaxed);
    // The std::memory_order_release is used in the case of success and causes the
    // above store to `node->m_next` to be visible after a load-acquire of m_head
    // in allocate that reads the value of this `node`.
    while (!m_head.compare_exchange_weak(node->m_next, node, std::memory_order_release))
      ;
  }
};

class SimpleSegregatedStorage : public SimpleSegregatedStorageBase
{
 public:                                // To be used with std::scoped_lock<std::mutex> from calling classes.
  std::mutex m_add_block_mutex;         // Protect against calling add_block concurrently.

 public:
  using SimpleSegregatedStorageBase::SimpleSegregatedStorageBase;

  bool try_allocate_more(std::function<bool()> const& add_new_block) override;
  void add_block(void* block, size_t block_size, size_t partition_size);
};

} // namespace memory
