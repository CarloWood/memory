// SPDX-FileCopyrightText: 2025 Carlo Wood
// SPDX-License-Identifier: MIT

#include "sys.h"
#include "MemoryPagePool.h"

namespace memory {

MemoryPagePool::MemoryPagePool(size_t block_size, blocks_t minimum_chunk_size, blocks_t maximum_chunk_size) :
  MemoryPagePoolBase(block_size),
  minimum_chunk_size_(minimum_chunk_size ? minimum_chunk_size : default_minimum_chunk_size()),
  maximum_chunk_size_(maximum_chunk_size ? maximum_chunk_size : default_maximum_chunk_size(minimum_chunk_size_))
{
  // minimum_chunk_size must be larger or equal than 1.
  ASSERT(minimum_chunk_size_ >= 1);
  // maximum_chunk_size must be larger or equal than minimum_chunk_size.
  ASSERT(maximum_chunk_size_ >= minimum_chunk_size_);

  DoutEntering(dc::memory, "MemoryPagePool::MemoryPagePool(" <<
      block_size << ", " << minimum_chunk_size << ", " << maximum_chunk_size << ") [" << this << "]");

  // This capacity is enough for allocating twice the maximum_chunk_size of memory (and then rounded up to the nearest power of two).
  chunks_.reserve(utils::nearest_power_of_two(1 + utils::log2(maximum_chunk_size_)));
  Dout(dc::memory, "The block size (" << block_size << " bytes) is " << (block_size / memory_page_size()) << " times the memory page size on this machine.");
  Dout(dc::memory, "The capacity of chunks_ is " << chunks_.capacity() << '.');
}

void MemoryPagePool::release()
{
  DoutEntering(dc::memory, "MemoryPagePool::release()");
  std::scoped_lock<std::mutex> lock(sss_.add_block_mutex_);
  // Wink out any remaining allocations.
  for (auto ptr : chunks_)
    std::free(ptr);
  Dout(dc::memory, "current size is " << (pool_blocks_ * block_size_) << " bytes.");
}

} // namespace memory

#ifdef CWDEBUG
NAMESPACE_DEBUG_CHANNELS_START
Channel memory("MEMORY");
NAMESPACE_DEBUG_CHANNELS_END
#endif
