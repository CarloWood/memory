/**
 * memory -- C++ Memory utilities
 *
 * @file
 * @brief Definition of class NodeMemoryResource.
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

#include "MemoryPagePool.h"
#include "SimpleSegregatedStorage.h"
#include "debug.h"

namespace memory {

// class NodeMemoryResource
//
// A fixed size memory resource that uses a MemoryPagePool as upstream.
// The block size is determined during runtime from the first allocation,
// which allows it to be used for allocators that allocate unknown types.
//
// Usage example:
//
//   memory::MemoryPagePool mpp(0x8000);                 // Serves chunks of 32 kB.
//   memory::NodeMemoryResource nmr(mpp);                // Serves chunks of unknown but fixed size (512 bytes in the case of a deque).
//   memory::DequeAllocator<AIStatefulTask*> alloc(nmr); // Wrapper around a pointer to memory::NodeMemoryResource,
//                                                       // providing an allocator interface.
//   std::deque<AIStatefulTask*, decltype(alloc)> test_deque(alloc);
//
// Note: it is possible to specify a block size upon construction (which obviously must be
// larger or equal to the actual (largest) block size that will be allocated).
//
class NodeMemoryResource
{
 public:
  // Create an uninitialized NodeMemoryResource. Call init() to initialize it.
  NodeMemoryResource() : mpp_(nullptr), block_size_(0) { }

  // Create an initialized NodeMemoryResource.
  NodeMemoryResource(MemoryPagePool& mpp, size_t block_size = 0) : mpp_(&mpp), block_size_(block_size)
  {
    DoutEntering(dc::notice, "NodeMemoryResource::NodeMemoryResource({" << (void*)mpp_ << "}, " << block_size << ") [" << this << "]");
  }

  // Destructor.
  ~NodeMemoryResource()
  {
    DoutEntering(dc::notice(mpp_), "NodeMemoryResource::~NodeMemoryResource() [" << this << "]");
  }

  // Late initialization.
  void init(MemoryPagePool* mpp_ptr, size_t block_size = 0)
  {
    // A NodeMemoryResource object may only be initialized once.
    ASSERT(mpp_ == nullptr);
    mpp_ = mpp_ptr;
    block_size_ = block_size;
    Dout(dc::notice(block_size > 0), "NodeMemoryResource::block_size_ using [" << mpp_ << "] set to " << block_size << " [" << this << "]");
  }

  void* allocate(size_t block_size)
  {
    //DoutEntering(dc::notice|continued_cf, "NodeMemoryResource::allocate(" << block_size << ") = ");
    size_t stored_block_size = block_size_.load(std::memory_order_relaxed);
    if (AI_UNLIKELY(stored_block_size == 0))
    {
      // No mutex is required here; it is not allowed to have a race condition between
      // two different block_size's. If different block sizes are used, then the largest
      // block_size must be used first, as in: the call to allocate(largest_block_size)
      // MUST already have returned before a call to allocate(smaller_block_size) may
      // happen. It is the responsibility of the user to make sure this is the case.
      //
      // Therefore we can assume here that any race conditions between multiple threads
      // calling this function while block_size_ is still 0 happen with the same value
      // of block_size.

      // Call NodeMemoryResource::init before using a default constructed NodeMemoryResource.
      // If this is inside a call to AIStatefulTaskMutex::lock then you probably forgot to create
      // a statefultask::DefaultMemoryPagePool object at the top of main. Go read the documentation
      // at the top of statefultask/DefaultMemoryPagePool.h.
      //
      // If this is inside a call to memory::DequeMemoryResource::allocate then you forgot to
      // construct a memory::DequeMemoryResource::Initialization object at the top of main.
      ASSERT(mpp_ != nullptr);
      block_size_.store(block_size, std::memory_order_relaxed);
      stored_block_size = block_size;
      Dout(dc::notice, "NodeMemoryResource::block_size_ using [" << mpp_ << "] set to " << block_size << " [" << this << "]");
    }
#ifdef CWDEBUG
    else
      ASSERT(block_size <= stored_block_size);
#endif
    void* ptr = sss_.allocate([this, stored_block_size](){
          void* chunk = mpp_->allocate();
          if (!chunk)
            return false;
          sss_.add_block(chunk, mpp_->block_size(), stored_block_size);
          return true;
        });
    //Dout(dc::finish, ptr);
    return ptr;
  }

  void deallocate(void* ptr)
  {
    //DoutEntering(dc::notice, "NodeMemoryResource::deallocate(" << ptr << ")");
    sss_.deallocate(ptr);
  }

 private:
  MemoryPagePool* mpp_;
  SimpleSegregatedStorage sss_;
  std::atomic<size_t> block_size_;
};

} // namespace memory

