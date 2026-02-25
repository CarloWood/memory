/**
 * memory -- C++ Memory utilities
 *
 * @file
 * @brief Definition of class NodeMemoryPool.
 *
 * @Copyright (C) 2018 - 2025 Carlo Wood.
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

#include <iosfwd>
#include <mutex>
#include <vector>
#include "debug.h"

namespace memory {
class NodeMemoryPool;
} // namespace memory

inline void* operator new(std::size_t size, memory::NodeMemoryPool& pool);

namespace memory {

struct Begin;
struct FreeList;

std::ostream& operator<<(std::ostream& os, NodeMemoryPool const& pool);

// class NodeMemoryPool
//
// This memory pool is intended for fixed size allocations (ie, of the same object),
// one object at a time; where the size and type of the object are not known until
// the first allocation.
//
// The reason for that is that it is intended to be used with std::allocate_shared or std::list.
//
// Usage:
//
// memory::NodeMemoryPool pool(64); // Will allocate 64 objects at a time.
//
// memory::Allocator<MyObject, memory::NodeMemoryPool> allocator(pool);
// std::shared_ptr<MyObject> ptr = std::allocate_shared<MyObject>(allocator, ...MyObject constructor arguments...);
//
// or (typically you shouldn't use the pool for both std::allocate_shared AND std::list,
// since they will allocate different sizes):
//
// std::list<MyObject, decltype(allocator)> my_list([optional other args...,] allocator);
//
// The Allocator CAN also be used to allocate objects of different types provided
// that their sizes are (about) the same. For example, if you want to allocate objects
// of size 32 and 30 bytes you could do:
//
// memory::Allocator<MyObject32, memory::NodeMemoryPool> allocator1(pool);
// memory::Allocator<MyObject30, memory::NodeMemoryPool> allocator2(pool);
//
// Just makes sure to FIRST allocate an object of the largest size; or
// make use of the pool constructor that sets the size in advance
// (this requires that you know the size in advance however):
//
// memory::NodeMemoryPool pool(64, 32); // Will allocate 64 objects of 32 bytes at a time.
//
// It is also possible to use this memory pool to replace heap allocation with new
// by adding an operator delete to an object and then using new with placement to
// create those objects. For example,
//
// class Foo {
//  public:
//   Foo(int);
//   void operator delete(void* ptr) { memory::NodeMemoryPool::static_free(ptr); }
// };
//
// and then create objects like
//
// memory::NodeMemoryPool pool(128, sizeof(Foo));
//
// Foo* foo = new(pool) Foo(42);        // Allocate memory from memory pool and construct object.
// delete foo;                          // Destruct object and return memory to the memory pool.
//
// NodeMemoryPool is thread-safe.

class NodeMemoryPool
{
 private:
  mutable std::mutex pool_mutex_;       // Protects the pool against concurrent accesses.

  size_t const nchunks_;                // The number of `size_' sized chunks to allocate at once. Should always be larger than 0.
  FreeList* free_list_;                 // The next free chunk, or nullptr if there isn't any left.
  std::vector<Begin*> blocks_;          // A list of all allocated blocks.
  size_t size_;                         // The (fixed) size of a single chunk in bytes.
                                        // alloc() always returns a chunk of this size except the first time when free_list_ is still 0.
  size_t total_free_;                   // The current total number of free chunks in the memory pool.

  friend void* ::operator new(std::size_t size, NodeMemoryPool& pool);
  void* alloc(size_t size);

 public:
  NodeMemoryPool(int nchunks, size_t chunk_size = 0) : nchunks_(nchunks), free_list_(nullptr), size_(chunk_size), total_free_(0) { }

  template<class Tp>
  Tp* malloc() { return static_cast<Tp*>(alloc(sizeof(Tp))); }

  void free(void* ptr);
  static void static_free(void* ptr);

  friend std::ostream& operator<<(std::ostream& os, NodeMemoryPool const& pool);
};

template<class Tp, class Mp>
struct Allocator
{
  Mp& memory_pool_;

  using value_type = Tp;
  size_t max_size() const { return 1; }
  Tp* allocate(std::size_t n);
  void deallocate(Tp* p, std::size_t DEBUG_ONLY(n)) { ASSERT(n == 1); memory_pool_.free(p); }
  Allocator(Mp& memory_pool) : memory_pool_(memory_pool) { }
  template<class T> Allocator(Allocator<T, Mp> const& other) : memory_pool_(other.memory_pool_) { }
  template<class T> bool operator==(Allocator<T, Mp> const& other) { return &memory_pool_ == &other.memory_pool_; }
  template<class T> bool operator!=(Allocator<T, Mp> const& other) { return &memory_pool_ != &other.memory_pool_; }
};

template<class Tp, class Mp>
Tp* Allocator<Tp, Mp>::allocate(std::size_t DEBUG_ONLY(n))
{
  // Only use this allocator with std::allocate_shared or std::list.
  ASSERT(n == 1);
  return memory_pool_.template malloc<Tp>();
}

} // namespace memory

inline void* operator new(std::size_t size, memory::NodeMemoryPool& pool) { return pool.alloc(size); }
