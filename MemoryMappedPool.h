/**
 * memory -- C++ Memory utilities
 *
 * @file
 * @brief Declaration of class MemoryMappedPool.
 *
 * @Copyright (C) 2025  Carlo Wood.
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
#include "MappedSegregatedStorage.h"
#include <filesystem>

namespace memory {

class MemoryMappedPool : public MemoryPagePoolBase
{
 protected:
  void* mapped_base_;           // The virtual address returned by mmap.
  size_t mapped_size_;          // The total size of the mapped memory.
  MappedSegregatedStorage mss_;

 public:
  enum class Mode
  {
    persistent,
    copy_on_write,
    read_only
  };

  MemoryMappedPool(std::filesystem::path const& filename, size_t block_size,
      size_t file_size = 0, Mode mode = Mode::persistent, bool zero_init = false);
  ~MemoryMappedPool() override;

  void* allocate() override { return mss_.allocate(mapped_base_, mapped_size_, block_size_); }
  void deallocate(void* ptr) override { mss_.deallocate(ptr); }

  blocks_t pool_blocks() { return pool_blocks_; }
};

} // namespace memory
