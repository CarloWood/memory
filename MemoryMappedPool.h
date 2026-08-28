// SPDX-FileCopyrightText: 2025 Carlo Wood
// SPDX-License-Identifier: MIT

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

  blocks_t pool_blocks() const { return pool_blocks_; }
  void* mapped_base() const { return mapped_base_; }
};

} // namespace memory
