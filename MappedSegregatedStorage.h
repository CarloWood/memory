/**
 * memory -- C++ Memory utilities
 *
 * @file
 * @brief Definition of class MappedSegregatedStorage.
 *
 * @Copyright (C) 2025  Carlo Wood.
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

#include "SimpleSegregatedStorage.h"

namespace memory {

class MappedSegregatedStorage : public SimpleSegregatedStorageBase
{
 public:
  void* allocate(void* mapped_base, size_t mapped_size, size_t block_size)
  {
    // Load the current value of m_head_tag into `head_tag`.
    // Use std::memory_order_acquire to synchronize with the std::memory_order_release in deallocate,
    // so that value of `next` read below will be the value written in deallocate corresponding to
    // this head value.
    PtrTag head_tag(this->m_head_tag.load(std::memory_order_acquire));
    while (head_tag != PtrTag::end_of_list)
    {
      PtrTag new_head_tag = head_tag.next();
      // If the next pointer is NULL then this could be a block that wasn't allocated before.
      // In that case the real next block is just the next block in the file.
      if (AI_UNLIKELY(new_head_tag.ptr() == nullptr))
      {
        char* front_node = reinterpret_cast<char*>(head_tag.ptr());
        char* second_node = front_node + block_size;
        new_head_tag = PtrTag::encode(second_node, head_tag.tag() + 1);
        if (AI_UNLIKELY(second_node == static_cast<char*>(mapped_base) + mapped_size))
          new_head_tag = PtrTag::end_of_list;
      }
      // The std::memory_order_acquire is used in case of failure and required for the next
      // read of m_next at the top of the current loop (the previous line).
      if (AI_LIKELY(this->CAS_head_tag(head_tag, new_head_tag, std::memory_order_acquire)))
        // Return the old head.
        return head_tag.ptr();
      // m_head_tag was changed (the new value is now in `head_tag`). Try again with the new value.
    }
    // Reached the end of the list.
    return nullptr;
  }
};

} // namespace memory
