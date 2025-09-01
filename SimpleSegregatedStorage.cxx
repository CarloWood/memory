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

#include "sys.h"
#include "SimpleSegregatedStorage.h"
#include "debug.h"

namespace memory {

bool SimpleSegregatedStorage::try_allocate_more(std::function<bool()> const& add_new_block)
{
  std::scoped_lock<std::mutex> lk(m_add_block_mutex);
  return this->m_head_tag.load(std::memory_order_relaxed) != PtrTag::end_of_list || add_new_block();
}

// Only call this from the lambda add_new_block that was passed to allocate.
void SimpleSegregatedStorage::add_block(void* block, size_t block_size, size_t partition_size)
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
    reinterpret_cast<typename PtrTag::FreeNode*>(node)->m_next = reinterpret_cast<typename PtrTag::FreeNode*>(next_node);
  }
  while (node != first_ptr);

  typename PtrTag::FreeNode* const first_node = reinterpret_cast<typename PtrTag::FreeNode*>(first_ptr);
  typename PtrTag::FreeNode* const last_node = reinterpret_cast<typename PtrTag::FreeNode*>(last_ptr);
  // Use a tag of zero because this is a completely new block anyway.
  PtrTag const new_head_tag{first_node, std::uintptr_t{0}};
  PtrTag head_tag(this->m_head_tag.load(std::memory_order_relaxed));
  do
  {
    last_node->m_next = head_tag.ptr();
  }
  while (!this->CAS_head_tag(head_tag, new_head_tag, std::memory_order_release));
}

} // namespace memory
