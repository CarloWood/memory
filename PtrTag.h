/**
 * memory -- C++ Memory utilities
 *
 * @file
 * @brief Definition of class PtrTag.
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

#include "utils/macros.h"
#include <cstdint>
#include "debug.h"

namespace memory {

struct PtrTag
{
  // A deallocated (free) node.
  struct FreeNode
  {
    FreeNode* next_;    // Points to the next free node, nullptr (the meaning of which depends on PtrTag).
  };

  std::uintptr_t encoded_;

  static constexpr std::uintptr_t tag_mask = 0x3;
  static constexpr std::uintptr_t ptr_mask = ~tag_mask;
  static constexpr std::uintptr_t end_of_list = tag_mask;

  static constexpr std::uintptr_t encode(void* ptr, uint32_t tag)
  {
    return std::bit_cast<std::uintptr_t>(ptr) | (tag & tag_mask);
  }

  FreeNode* ptr() const { return reinterpret_cast<FreeNode*>(encoded_ & ptr_mask); }
  std::uintptr_t tag() const { return encoded_ & tag_mask; }

  PtrTag(std::uintptr_t encoded) : encoded_(encoded) { }
  PtrTag(FreeNode* node, std::uintptr_t tag) : encoded_(node ? PtrTag::encode(node, tag) : end_of_list) { }

  PtrTag next() const
  {
    FreeNode* front_node = ptr();
    FreeNode* second_node = front_node->next_;
    return {second_node, tag() + 1};
  }

  bool operator!=(std::uintptr_t encoded) const { return encoded_ != encoded; }
};

} // namespace memory
