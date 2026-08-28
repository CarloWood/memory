// SPDX-FileCopyrightText: 2025 Carlo Wood
// SPDX-License-Identifier: MIT

#pragma once

#include "SimpleSegregatedStorage.h"

namespace memory {

class MappedSegregatedStorage : public SimpleSegregatedStorageBase
{
 public:
  void* allocate(void* mapped_base, size_t mapped_size, size_t block_size)
  {
    // Load the current value of head_tag_ into `head_tag`.
    // Use std::memory_order_acquire to synchronize with the std::memory_order_release in deallocate,
    // so that value of `next` read below will be the value written in deallocate corresponding to
    // this head value.
    PtrTag head_tag(this->head_tag_.load(std::memory_order_acquire));
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
      // read of next_ at the top of the current loop (the previous line).
      if (AI_LIKELY(this->CAS_head_tag(head_tag, new_head_tag, std::memory_order_acquire)))
        // Return the old head.
        return head_tag.ptr();
      // head_tag_ was changed (the new value is now in `head_tag`). Try again with the new value.
    }
    // Reached the end of the list.
    return nullptr;
  }
};

} // namespace memory
