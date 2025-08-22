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

#include <atomic>
#include <functional>
#include <mutex>
#include "debug.h"

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


// SimpleSegregatedStorage
//
// Maintains an unordered free list of blocks.
//
class SimpleSegregatedStorage
{
  struct FreeNode { FreeNode* m_next; };

 private:
  std::atomic<FreeNode*> m_head;        // Points to the first free memory block in the free list, or nullptr if the free list is empty.
 public:                                // To be used with std::scoped_lock<std::mutex> from calling classes.
  std::mutex m_add_block_mutex;         // Protect against calling add_block concurrently.

 public:
  // Construct an empty free list.
  SimpleSegregatedStorage() : m_head(nullptr) { }

  void* allocate(std::function<bool()> const& add_new_block)
  {
    for (;;)
    {
      FreeNode* node = m_head.load(std::memory_order_relaxed);
      if (AI_UNLIKELY(!node))
      {
        if (!try_allocate_more(add_new_block))
          return nullptr;
        continue;
      }
      while (AI_UNLIKELY(!m_head.compare_exchange_weak(node, node->m_next, std::memory_order_release, std::memory_order_relaxed) && node))
        ;
      if (AI_LIKELY(node))
        return node;
    }
  }

  // ptr must be a value previously returned by allocate().
  void deallocate(void* ptr)
  {
    FreeNode* node = static_cast<FreeNode*>(ptr);
    node->m_next = m_head.load(std::memory_order_relaxed);
    while (!m_head.compare_exchange_weak(node->m_next, node, std::memory_order_release, std::memory_order_relaxed))
      ;
  }

  bool try_allocate_more(std::function<bool()> const& add_new_block)
  {
    std::scoped_lock<std::mutex> lk(m_add_block_mutex);
    return m_head.load(std::memory_order_relaxed) != nullptr || add_new_block();
  }

  // Only call this from the lambda add_new_block that was passed to allocate.
  void add_block(void* block, size_t block_size, size_t partition_size)
  {
    unsigned int const number_of_partitions = block_size / partition_size;

    // block_size must be a multiple of partition_size (at least 2 times).
    ASSERT(number_of_partitions > 1);

    char* const first_ptr = static_cast<char*>(block);
    char* const last_ptr = first_ptr + (number_of_partitions - 1) * partition_size;     // > first_ptr, see ASSERT.
    char* node = last_ptr;
    do
    {
      char* next_node = node;
      node = next_node - partition_size;
      reinterpret_cast<FreeNode*>(node)->m_next = reinterpret_cast<FreeNode*>(next_node);
    }
    while (node != first_ptr);
    FreeNode* first_node = reinterpret_cast<FreeNode*>(first_ptr);
    FreeNode* last_node = reinterpret_cast<FreeNode*>(last_ptr);
    last_node->m_next = m_head.load(std::memory_order_relaxed);
    while (!m_head.compare_exchange_weak(last_node->m_next, first_node, std::memory_order_release, std::memory_order_relaxed))
      ;
  }
};

} // namespace memory
