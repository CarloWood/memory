// SPDX-FileCopyrightText: 2019, 2025 Carlo Wood
// SPDX-License-Identifier: MIT

#include "sys.h"
#include "SimpleSegregatedStorage.h"
#include "debug.h"

#ifdef CWDEBUG
NAMESPACE_DEBUG_CHANNELS_START
extern Channel memory;
NAMESPACE_DEBUG_CHANNELS_END
#endif

namespace memory {

bool SimpleSegregatedStorage::try_allocate_more(std::function<bool()> const& add_new_block)
{
  std::scoped_lock<std::mutex> lk(add_block_mutex_);
  return this->head_tag_.load(std::memory_order_relaxed) != PtrTag::end_of_list || add_new_block();
}

// Only call this from the lambda add_new_block that was passed to allocate.
void SimpleSegregatedStorage::add_block(void* block, size_t block_size, size_t partition_size)
{
  DoutEntering(dc::memory, "SimpleSegregatedStorage::add_block(" << block << ", " << block_size << ", " << partition_size << ")");

  unsigned int const number_of_partitions = block_size / partition_size;
  Dout(dc::memory, "Creating a free list of " << number_of_partitions <<
      " partitions in the memory range [" << block << ", " << (void*)((char*)block + block_size) << ").");

  // block_size must be a multiple of partition_size (at least 2 times).
  ASSERT(number_of_partitions > 1);

  char* const first_ptr = static_cast<char*>(block);
  char* const last_ptr = first_ptr + (number_of_partitions - 1) * partition_size;     // > first_ptr, see ASSERT.
  char* node = last_ptr;
  do
  {
    char* next_node = node;
    node = next_node - partition_size;
    reinterpret_cast<typename PtrTag::FreeNode*>(node)->next_ = reinterpret_cast<typename PtrTag::FreeNode*>(next_node);
  }
  while (node != first_ptr);

  typename PtrTag::FreeNode* const first_node = reinterpret_cast<typename PtrTag::FreeNode*>(first_ptr);
  typename PtrTag::FreeNode* const last_node = reinterpret_cast<typename PtrTag::FreeNode*>(last_ptr);
  // Use a tag of zero because this is a completely new block anyway.
  PtrTag const new_head_tag{first_node, std::uintptr_t{0}};
  PtrTag head_tag(this->head_tag_.load(std::memory_order_relaxed));
  do
  {
    last_node->next_ = head_tag.ptr();
  }
  while (!this->CAS_head_tag(head_tag, new_head_tag, std::memory_order_release));
}

} // namespace memory
