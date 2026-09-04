// SPDX-FileCopyrightText: 2019, 2025 Carlo Wood
// SPDX-License-Identifier: MIT

#pragma once

#include "utils/macros.h"                       // AI_UNLIKELY
#include "utils/log2.h"                         // utils::log2
#include "utils/nearest_power_of_two.h"         // utils::nearest_power_of_two
#include "SimpleSegregatedStorage.h"
#include <algorithm>
#include <mutex>
#include <unistd.h>
#include "debug.h"

#ifdef CWDEBUG
NAMESPACE_DEBUG_CHANNELS_START
extern Channel memory;
NAMESPACE_DEBUG_CHANNELS_END
#endif

namespace memory {
namespace details {

// Helper class to provide memory_page_size().
struct MemoryPageSize
{
  static size_t memory_page_size()
  {
    static size_t const memory_page_size_ = sysconf(_SC_PAGE_SIZE);
    return memory_page_size_;
  }
};

} // namespace details

class MemoryPagePoolBase : public details::MemoryPageSize
{
 public:
  using blocks_t = unsigned int;

 protected:
  size_t const block_size_;             // The size of a block as returned by allocate(), in bytes.
  blocks_t pool_blocks_;                // The total amount of available memory, in blocks.

 protected:
  MemoryPagePoolBase(size_t block_size) : block_size_(block_size), pool_blocks_(0) { }

  virtual ~MemoryPagePoolBase() = default;

 public:
  // Accessor.
  size_t block_size() const { return block_size_; }

  virtual void* allocate() = 0;
  virtual void deallocate(void* ptr) = 0;
};

// A memory pool that returns fixed-size memory blocks allocated with std::aligned_alloc and aligned to memory_page_size.
//
class MemoryPagePool : public MemoryPagePoolBase
{
 public:
  static constexpr size_t default_block_size = 0x8000;

 protected:
  SimpleSegregatedStorage sss_;
  blocks_t const minimum_chunk_size_;  // The minimum size of internally allocated contiguous memory blocks, in blocks.
  blocks_t const maximum_chunk_size_;  // The maximum size of internally allocated contiguous memory blocks, in blocks.
  std::vector<void*> chunks_;          // All allocated chunks that were allocated with std::aligned_alloc.

 protected:
  virtual blocks_t default_minimum_chunk_size() { return 2; }
  virtual blocks_t default_maximum_chunk_size(blocks_t UNUSED_ARG(minimum_chunk_size)) { return 1024; }

 public:
  MemoryPagePool(size_t block_size = default_block_size,        // The size of a block as returned by allocate(), in bytes;
                                                                // must be a multiple of the memory page size.
                 blocks_t minimum_chunk_size = 0,               // A value of 0 will use the value returned by default_minimum_chunk_size().
                 blocks_t maximum_chunk_size = 0);              // A value of 0 will use the value returned by
                                                                // default_maximum_chunk_size(minimum_chunk_size).

  ~MemoryPagePool() override
  {
    DoutEntering(dc::memory, "MemoryPagePool::~MemoryPagePool() [" << this << "]");
    release();
  }

  void* allocate() override
  {
    DoutEntering(dc::memory, "MemoryPagePool::allocate() [" << this << "]");
    return sss_.allocate([this](){
        // This runs in the critical area of SimpleSegregatedStorage::add_block_mutex_.
        blocks_t extra_blocks = std::clamp(pool_blocks_, minimum_chunk_size_, maximum_chunk_size_);
        size_t extra_size = extra_blocks * block_size_;
        Dout(dc::memory, "MemoryPagePool::allocate: allocating " << extra_blocks << " extra blocks of memory (" << extra_size << " bytes).");
        void* chunk = std::aligned_alloc(memory_page_size(), extra_size);
        if (AI_UNLIKELY(chunk == nullptr))
          return false;
        sss_.add_block(chunk, extra_size, block_size_);
        pool_blocks_ += extra_blocks;
        chunks_.push_back(chunk);
        return true;
    });
  }

  void deallocate(void* ptr) override
  {
    sss_.deallocate(ptr);
  }

  void release();

  blocks_t pool_blocks() { std::scoped_lock<std::mutex> lock(sss_.add_block_mutex_); return pool_blocks_; }
};

} // namespace memory
