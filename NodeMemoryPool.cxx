/**
 * memory -- C++ Memory utilities
 *
 * @file
 * @brief Definition of class NodeMemoryPool
 *
 * @Copyright (C) 2018 - 2025  Carlo Wood.
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
#include "NodeMemoryPool.h"
#include "utils/macros.h"               // AI_UNLIKELY
#include "utils/is_power_of_two.h"      // utils::is_power_of_two
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include "debug.h"

namespace memory {

union Next
{
  size_t n;                     // This must have the same size as a pointer, so that setting this to zero causes ptr == nullptr.
  FreeList* ptr;
};

struct FreeList
{
  ssize_t* free;                // Points to Begin::free of the current block.
  Next next_;                  // next_.ptr either points to the next free chunk in the free list, or is nullptr when there are no free chunks left.
};

struct Allocated
{
  ssize_t* free;
  char data[];                  // size_ bytes of data.
};

union Chunk
{
  FreeList free_list;
  Allocated allocated;
};

struct Begin
{
  ssize_t free;                 // Each allocated memory block (of nchunks_ chunks of size_ each) begins with a size_t
                                // that counts the number of free chunks in the block.
  NodeMemoryPool* pool;         // A pointer to the pool object to support overloading operator delete for objects.
  Chunk first_chunk;            // Subsequently there is a Chunk object, of which this is the first,
                                // every offsetof(Allocated, data) + size_ bytes (aka the real size of "Allocated",
                                // aka the real size of Chunk, where sizeof(FreeList) needs to be less than or equal
                                // the size of Allocated. In other words size_ >= sizeof(Next)).
};

static_assert(offsetof(FreeList, next_) == offsetof(Allocated, data), "Unexpected alignment.");

#if defined(CWDEBUG) || defined(DEBUG)
static_assert(alignof(Chunk) == alignof(size_t), "Unexpected alignment of Chunk.");     // Because we shift all Chunk`s by a size_t (technically, alignof(size_t)
                                                                                        // should be a multiple of alignof(Chunk)).
static_assert(utils::is_power_of_two(alignof(Chunk)), "Alignment of Chunk is expected to be a power of 2.");
static constexpr size_t chunk_align_mask = alignof(Chunk) - 1;
#endif

void* NodeMemoryPool::alloc(size_t size)
{
  std::unique_lock<std::mutex> lock(pool_mutex_);
  FreeList* ptr = free_list_;
  if (AI_UNLIKELY(!ptr))
  {
    if (AI_UNLIKELY(!size_))
      size_ = size;    // If size_ wasn't initialized yet, set it to the size of the first allocation.
    // size_ must be greater or equal sizeof(Next), and a multiple of alignof(Chunk).
    ASSERT(size_ >= sizeof(Next) && (size_ & chunk_align_mask) == 0);
    // Allocate space for Begin::free plus Begin::pool followed by nchunks_ of size_ (offsetof(Allocated, data) + size_) (the real size of Allocated).
    Dout(dc::notice, "NodeMemoryPool::alloc: allocating " << (offsetof(Begin, first_chunk) + nchunks_ * (offsetof(Allocated, data) + size_)) << " bytes of memory [" << (void*)this << "].");
    Begin* begin = static_cast<Begin*>(std::malloc(offsetof(Begin, first_chunk) + nchunks_ * (offsetof(Allocated, data) + size_)));
    begin->pool = this;
    ptr = free_list_ = &begin->first_chunk.free_list;
    ptr->next_.n = nchunks_ - 1;
    ptr->free = &begin->free;
    *ptr->free = nchunks_;
    blocks_.push_back(begin);
    total_free_ += nchunks_;
  }
  // size must fit. If you use multiple sizes, allocate the largest size first.
  ASSERT(size <= size_);
  if (AI_UNLIKELY(ptr->next_.n < nchunks_ && ptr->next_.ptr))
  {
    size_t n = ptr->next_.n;
    ptr->next_.ptr = reinterpret_cast<FreeList*>(reinterpret_cast<char*>(ptr) + size_ + offsetof(FreeList, next_));
    ASSERT(ptr->next_.n >= nchunks_); // Smaller values are used as 'magic' values.
    ptr->next_.ptr->next_.n = n - 1;
    ptr->next_.ptr->free = ptr->free;
  }
  free_list_ = ptr->next_.ptr;
  --*ptr->free;
  --total_free_;
  ASSERT(*ptr->free >= 0);
  return reinterpret_cast<Chunk*>(ptr)->allocated.data;
}

void NodeMemoryPool::free(void* p)
{
  // Interpret the pointer p as pointing to Chunk::allocated::data and reinterpret/convert it to a pointer to Chunk::free_list.
  FreeList* ptr = reinterpret_cast<FreeList*>(reinterpret_cast<char*>(p) - offsetof(Allocated, data));
  std::unique_lock<std::mutex> lock(pool_mutex_);
  ptr->next_.ptr = free_list_;
  free_list_ = ptr;
  ++*ptr->free;
  ++total_free_;
  ASSERT(*ptr->free <= (ssize_t)nchunks_);
  if (AI_UNLIKELY(*ptr->free == (ssize_t)nchunks_) && total_free_ >= 2 * nchunks_)
  {
    // The last chunk of this block was freed; delete it.
    // Find begin and end of the block.
    char* const begin = reinterpret_cast<char*>(ptr->free) - offsetof(Begin, free);                             // The actual start of the allocated memory block (Begin*).
    char* const end = begin + offsetof(Begin, first_chunk) + nchunks_ * (offsetof(Allocated, data) + size_);  // One passed the end of the allocated memory block.
    // Run over the whole free list. Note that when FreeList* == nullptr we reached the end of the free list.
    // However, if a pointer value interpreted as a size_t is less than nchunks_ then that means that the
    // next size_t chunks are free chunks and then the free list ends.
    // free_list_ itself always points to the first free chunk, so the first test is always true.
    for (FreeList** fpp = &free_list_; reinterpret_cast<size_t>(*fpp) >= nchunks_; fpp = &(*fpp)->next_.ptr)
    {
      // When *fpp points inside the block that we're about to delete, then just skip it until we
      // either reach the end of the free list (*fpp == nullptr) or reinterpret_cast<size_t>(*fpp) is
      // less than nchunks_ (in which case it will not be >= begin either).
      while (begin <= reinterpret_cast<char*>(*fpp) && reinterpret_cast<char*>(*fpp) < end)
        *fpp = (*fpp)->next_.ptr;
      // Hence, if at this point reinterpret_cast<size_t>(*fpp) < nchunks_, then that happened
      // because *fpp was just assigned that value in the above while loop, which means that
      // all remaining chunks are also part of the deleted block. And we need to set *fpp to nullptr.
      if (reinterpret_cast<size_t>(*fpp) < nchunks_)
      {
        // We reached the end of the free list.
        *fpp = nullptr; // Any remaining "free" chunk count refers to the block that is being deleted.
        break;
      }
    }
    total_free_ -= nchunks_;
    std::free(begin);
    blocks_.erase(std::remove(blocks_.begin(), blocks_.end(), reinterpret_cast<Begin*>(begin)));
  }
}

//static
void NodeMemoryPool::static_free(void* ptr)
{
  Allocated* allocated = reinterpret_cast<Allocated*>(reinterpret_cast<char*>(ptr) - offsetof(Allocated, data));
  NodeMemoryPool* self = reinterpret_cast<Begin*>(allocated->free)->pool;
  self->free(ptr);
}

std::ostream& operator<<(std::ostream& os, NodeMemoryPool const& pool)
{
  std::unique_lock<std::mutex> lock(pool.pool_mutex_);
  size_t allocated_size = (offsetof(Begin, first_chunk) + pool.nchunks_ * (offsetof(Allocated, data) + pool.size_)) * pool.blocks_.size();
  size_t num_chunks = pool.nchunks_ * pool.blocks_.size();
  size_t num_free_chunks = 0;
  for (Begin* begin : pool.blocks_)
    num_free_chunks += begin->free;
  ASSERT(num_free_chunks == pool.total_free_);
  os << "NodeMemoryPool stats: node size: " << pool.size_ << "; allocated size: " << allocated_size <<
      "; total/used/free: " << num_chunks << '/' << (num_chunks - num_free_chunks) << '/' << num_free_chunks;
  return os;
}

} // namespace memory
