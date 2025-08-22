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
  mutable std::mutex m_pool_mutex;      // Protects the pool against concurrent accesses.

  size_t const m_nchunks;               // The number of `m_size' sized chunks to allocate at once. Should always be larger than 0.
  FreeList* m_free_list;                // The next free chunk, or nullptr if there isn't any left.
  std::vector<Begin*> m_blocks;         // A list of all allocated blocks.
  size_t m_size;                        // The (fixed) size of a single chunk in bytes.
                                        // alloc() always returns a chunk of this size except the first time when m_free_list is still 0.
  size_t m_total_free;                  // The current total number of free chunks in the memory pool.

  friend void* ::operator new(std::size_t size, NodeMemoryPool& pool);
  void* alloc(size_t size);

 public:
  NodeMemoryPool(int nchunks, size_t chunk_size = 0) : m_nchunks(nchunks), m_free_list(nullptr), m_size(chunk_size), m_total_free(0) { }

  template<class Tp>
  Tp* malloc() { return static_cast<Tp*>(alloc(sizeof(Tp))); }

  void free(void* ptr);
  static void static_free(void* ptr);

  friend std::ostream& operator<<(std::ostream& os, NodeMemoryPool const& pool);
};

template<class Tp, class Mp>
struct Allocator
{
  Mp& m_memory_pool;

  using value_type = Tp;
  size_t max_size() const { return 1; }
  Tp* allocate(std::size_t n);
  void deallocate(Tp* p, std::size_t DEBUG_ONLY(n)) { ASSERT(n == 1); m_memory_pool.free(p); }
  Allocator(Mp& memory_pool) : m_memory_pool(memory_pool) { }
  template<class T> Allocator(Allocator<T, Mp> const& other) : m_memory_pool(other.m_memory_pool) { }
  template<class T> bool operator==(Allocator<T, Mp> const& other) { return &m_memory_pool == &other.m_memory_pool; }
  template<class T> bool operator!=(Allocator<T, Mp> const& other) { return &m_memory_pool != &other.m_memory_pool; }
};

template<class Tp, class Mp>
Tp* Allocator<Tp, Mp>::allocate(std::size_t DEBUG_ONLY(n))
{
  // Only use this allocator with std::allocate_shared or std::list.
  ASSERT(n == 1);
  return m_memory_pool.template malloc<Tp>();
}

} // namespace memory

inline void* operator new(std::size_t size, memory::NodeMemoryPool& pool) { return pool.alloc(size); }
