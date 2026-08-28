// SPDX-FileCopyrightText: 2025 Carlo Wood
// SPDX-License-Identifier: MIT

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
