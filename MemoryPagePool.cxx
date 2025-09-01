/**
 * memory -- C++ Memory utilities
 *
 * @file
 * @brief Definition of class MemoryPagePool.
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
#include "MemoryPagePool.h"

namespace memory {

MemoryPagePool::MemoryPagePool(size_t block_size, blocks_t minimum_chunk_size, blocks_t maximum_chunk_size) :
  MemoryPagePoolBase(block_size),
  m_minimum_chunk_size(minimum_chunk_size ? minimum_chunk_size : default_minimum_chunk_size()),
  m_maximum_chunk_size(maximum_chunk_size ? maximum_chunk_size : default_maximum_chunk_size(m_minimum_chunk_size))
{
  // minimum_chunk_size must be larger or equal than 1.
  ASSERT(m_minimum_chunk_size >= 1);
  // maximum_chunk_size must be larger or equal than minimum_chunk_size.
  ASSERT(m_maximum_chunk_size >= m_minimum_chunk_size);

  DoutEntering(dc::notice, "MemoryPagePool::MemoryPagePool(" <<
      block_size << ", " << minimum_chunk_size << ", " << maximum_chunk_size << ") [" << this << "]");

  // This capacity is enough for allocating twice the maximum_chunk_size of memory (and then rounded up to the nearest power of two).
  m_chunks.reserve(utils::nearest_power_of_two(1 + utils::log2(m_maximum_chunk_size)));
  Dout(dc::notice, "The block size (" << block_size << " bytes) is " << (block_size / memory_page_size()) << " times the memory page size on this machine.");
  Dout(dc::notice, "The capacity of m_chunks is " << m_chunks.capacity() << '.');
}

void MemoryPagePool::release()
{
  DoutEntering(dc::notice, "MemoryPagePool::release()");
  std::scoped_lock<std::mutex> lock(m_sss.m_add_block_mutex);
  // Wink out any remaining allocations.
  for (auto ptr : m_chunks)
    std::free(ptr);
  Dout(dc::notice, "current size is " << (m_pool_blocks * m_block_size) << " bytes.");
}

} // namespace memory
