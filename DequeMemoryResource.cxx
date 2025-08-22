/**
 * memory -- C++ Memory utilities
 *
 * @file
 * @brief Definition of class DequeMemoryResource.
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
#include "NodeMemoryResource.h"
#include "DequeMemoryResource.h"
#include "utils/log2.h"
#include <cstdlib>

namespace memory {

//static
DequeMemoryResource DequeMemoryResource::s_instance;

namespace {

// Map m_node_memory_resources array index to the block size that it must store.
constexpr std::array<std::size_t, DequeMemoryResource::nmra_size> const i2s = { 8, 12, 18, 26, 38, 54, 78, 111, 158, 224, 318, 451 };

// Convert an index to its size.
constexpr std::size_t index_to_size(int n) { return sizeof(void*) * i2s[n]; }

// The largest size for which we still use a NodeMemoryResource.
constexpr std::size_t upper_size = index_to_size(i2s.size() - 1);

} // namespace

void* DequeMemoryResource::allocate(std::size_t number_of_bytes)
{
  DoutEntering(dc::notice, "DequeMemoryResource::allocate(" << number_of_bytes << ") ; " << (number_of_bytes / sizeof(void*)));

  // Make small values of index the fast path.
  if (AI_UNLIKELY(number_of_bytes > upper_size))
    return malloc(number_of_bytes);

  int const index = size_to_index(number_of_bytes);

  Dout(dc::notice, "DequeMemoryResource::allocate(" << number_of_bytes << ") is using index " << index << " / " << (nmra_size - 1));
  return m_node_memory_resources[index].allocate(number_of_bytes);
}

void DequeMemoryResource::deallocate(void* p, std::size_t number_of_bytes)
{
  // Marked "unlikely" because the smallest sizes should be the fast path.
  if (AI_UNLIKELY(number_of_bytes > upper_size))
  {
    free(p);
    return;
  }

  m_node_memory_resources[size_to_index(number_of_bytes)].deallocate(p);
  return;
}

void DequeMemoryResource::init(MemoryPagePool* mpp_ptr)
{
  for (int index = 0; index < m_node_memory_resources.size(); ++index)
    m_node_memory_resources[index].init(mpp_ptr, index_to_size(index));
}

} // namespace memory
