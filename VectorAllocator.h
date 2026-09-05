// SPDX-FileCopyrightText: 2026 Carlo Wood
// SPDX-License-Identifier: MIT

#include "NodeMemoryResource.h"
#include "utils/is_power_of_two.h"
#include "utils/log2.h"
#include "utils/macros.h"
#include "utils/nearest_power_of_two.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>        // std::once_flag
#include <new>
#include <stdexcept>
#include "debug.h"      // ASSERT

#pragma once

namespace memory {

// These allocations come from a NodeMemoryResource - they are at most several memory pages and do not need more than the size of an int (32 bits).
using allocation_size_type = uint_fast32_t;

namespace detail {
static constexpr allocation_size_type default_alignment = sizeof(std::size_t);
static constexpr allocation_size_type default_smallest_allocation = default_alignment;

template <allocation_size_type mpp_block_size, allocation_size_type smallest_allocation>
struct MaxToLargest
{
  static_assert(smallest_allocation < mpp_block_size, "largest_allocation must become larger than smallest_allocation");
  static constexpr allocation_size_type largest_allocation =
      (allocation_size_type{1} << utils::log2(mpp_block_size / smallest_allocation)) * smallest_allocation;
};
} // namespace detail

template <allocation_size_type smallest_allocation, allocation_size_type largest_allocation, allocation_size_type alignment = sizeof(std::size_t)>
class GeometricMemoryResource
{
  using allocation_class_type = uint_least16_t;
  using nmr_index_type = int;                           // The type returned by utils::log2.

  // Sanity checks.
  static_assert(utils::is_power_of_two(alignment), "alignment must be a power of two");
  static_assert(alignment < 32, "GeometricMemoryResource is not suited for large alignments");
  static_assert(smallest_allocation > 0);
  static_assert(smallest_allocation < largest_allocation, "GeometricMemoryResource requires at least one NodeMemoryResource size class");
  static_assert(smallest_allocation % alignment == 0, "smallest_allocation must be a multiple of alignment");

  // More sanity checks.
  static_assert(std::numeric_limits<allocation_class_type>::max() >= largest_allocation / smallest_allocation);

 protected:
  // allocation_size must be smallest_allocation times a power of two and not larger than largest_allocation.
  static constexpr nmr_index_type allocation_size_to_nmr_index(allocation_size_type allocation_size)
  {
    // Call this function only on values returned by elements_to_allocation_size.
    ASSERT(smallest_allocation <= allocation_size && allocation_size <= largest_allocation);
    allocation_class_type const allocation_class = static_cast<allocation_class_type>(allocation_size / smallest_allocation);
    ASSERT(utils::is_power_of_two(allocation_class));
    return utils::log2(allocation_class);
  }

 private:
  // The largest allocation does not get an index because a NodeMemoryResource does not support a block with a single partition in it.
  static constexpr nmr_index_type number_of_allocation_sizes = allocation_size_to_nmr_index(largest_allocation);

 protected:
  // Contains the NodeMemoryResource's for the size classes {smallest_allocation, ..., largest_allocation / 2}.
  static std::array<memory::NodeMemoryResource, number_of_allocation_sizes> nmrs_;

 private:
  static std::once_flag initialize_nmrs_once_;

 public:
  // Bind the shared size-class resources to mpp the first time this allocator specialization is constructed.
  //
  // The pool must outlive every allocator and allocation of this specialization. Concurrent construction is safe;
  // the first supplied pool remains the upstream resource for all later allocator copies and constructions.
  GeometricMemoryResource(MemoryPagePool& mpp)
  {
    std::call_once(initialize_nmrs_once_, [&mpp] {
      allocation_size_type allocation_size = smallest_allocation;
      for (NodeMemoryResource& nmr : nmrs_)
      {
        // Note that this doesn't allocate any memory pages yet. That only happens once a NodeMemoryResource is first used.
        nmr.init(&mpp, allocation_size);
        allocation_size *= 2;
      }
    });
  }
};

template <allocation_size_type smallest_allocation, allocation_size_type largest_allocation, allocation_size_type alignment>
std::array<memory::NodeMemoryResource, GeometricMemoryResource<smallest_allocation, largest_allocation, alignment>::number_of_allocation_sizes>
    GeometricMemoryResource<smallest_allocation, largest_allocation, alignment>::nmrs_;

template <allocation_size_type smallest_allocation, allocation_size_type largest_allocation, allocation_size_type alignment>
std::once_flag GeometricMemoryResource<smallest_allocation, largest_allocation, alignment>::initialize_nmrs_once_;

template <allocation_size_type smallest_allocation, allocation_size_type largest_allocation, allocation_size_type alignment>
constexpr bool operator==(GeometricMemoryResource<smallest_allocation, largest_allocation, alignment> const&,
                          GeometricMemoryResource<smallest_allocation, largest_allocation, alignment> const&) noexcept
{
  return true;
}

// class VectorAllocator
//
// This allocator is intended to be used with std::vector.
//
// Allocation requests are rounded up to the next supported size class and served by the pool associated with that class.
// The smallest supported size class is given by `smallest_allocation`; each subsequent size class is twice as large.
// The largest supported size class is given by `largest_allocation`, defined by:
//
//     largest_allocation <= mpp_block_size < 2 * largest_allocation.
//
// The existing size classes are then
//
//     {smallest_allocation, smallest_allocation * 2, smallest_allocation * 4, smallest_allocation * 8, ..., largest_allocation}
//                                                                                                                   ^
//                                                                                                                   |__ actually mpp_block_size.
//
// where `largest_allocation` is twice as large as the previous class size: `smallest_allocation` times some power of two.
// However, because only a single block with size `largest_allocation` fits in a memory pool block of size `mpp_block_size`,
// we can't use a NodeMemoryResource for that (which requires the memory pool block to be partitioned into at least two partitions).
// Therefore a memory pool block (of size `mpp_block_size`) is returned directly for that case.
//
// An allocation request larger than mpp_block_size is served by std::allocator.
//
// Allocations of size 0 return nullptr. Deallocating nullptr is a no-op.
//
template <typename T, allocation_size_type smallest_allocation = detail::default_smallest_allocation,
          allocation_size_type mpp_block_size = memory::MemoryPagePool::default_block_size,
          allocation_size_type alignment = detail::default_alignment>
class VectorAllocator
    : public GeometricMemoryResource<smallest_allocation, detail::MaxToLargest<mpp_block_size, smallest_allocation>::largest_allocation, alignment>
{
 public:
  static constexpr std::size_t element_size = sizeof(T);
  static constexpr allocation_size_type largest_allocation = detail::MaxToLargest<mpp_block_size, smallest_allocation>::largest_allocation;
  using Base_ = GeometricMemoryResource<smallest_allocation, largest_allocation, alignment>;

  static constexpr std::size_t allocation_size_to_elements(allocation_size_type size) { return size / element_size; }

  // And the inverse of that.
  static constexpr allocation_size_type elements_to_allocation_size(std::size_t elements)
  {
    // This function must return smallest_allocation times a power of two.
    return smallest_allocation * std::max(std::size_t{1}, utils::nearest_power_of_two(elements * element_size / smallest_allocation));
  }

  // Trying to allocate more elements than this makes VectorAllocator fall back to std::allocator.
  // We use mpp_block_size here because that is the largest block returned.
  static constexpr std::size_t maximum_number_of_elements = allocation_size_to_elements(mpp_block_size);

 public:
  using value_type = T;
  using pointer = value_type*;
  using size_type = std::size_t;

  template <typename U>
  struct rebind
  {
    static_assert(std::same_as<U, value_type>, "VectorAllocator can allocate only its element type");
    using other = VectorAllocator;
  };

  VectorAllocator(memory::MemoryPagePool& mpp) : GeometricMemoryResource<smallest_allocation, largest_allocation, alignment>(mpp)
  {
    // mpp_block_size must be the same as the size that was passed to mpp.
    if (mpp_block_size != mpp.block_size())
      throw std::invalid_argument("VectorAllocator: MemoryPagePool block size does not match mpp_block_size");
  }

 private:
  using Base_::allocation_size_to_nmr_index;
  using Base_::nmrs_;

 public:
  // Allocate uninitialized storage for n value_type objects from the shared resource for its power-of-two size class.
  //
  // Throws std::bad_alloc when the upstream page pool cannot supply another block.
  value_type* allocate(std::size_t n)
  {
    // A zero-sized allocation must be supported by a conforming allocator. The result is unspecified; we choose to return nullptr.
    if (AI_UNLIKELY(n == 0))
      return nullptr;
    if (AI_UNLIKELY(n > maximum_number_of_elements))
      return std::allocator<value_type>{}.allocate(n);

    allocation_size_type const allocation_size = elements_to_allocation_size(n);
    int const index = allocation_size_to_nmr_index(allocation_size);
    Dout(dc::memory, "Allocating " << (index == nmrs_.size() ? mpp_block_size : allocation_size) << " bytes from index " << index << ".");
    // Note: with 0 < n <= maximum_number_of_elements, allocation_size will be between elements_to_allocation_size(1) == smallest_allocation
    // and elements_to_allocation_size(maximum_number_of_elements) == largest_allocation, and therefore index will be between
    // allocation_size_to_nmr_index(smallest_allocation) == 0 and
    // allocation_size_to_nmr_index(largest_allocation) == number_of_allocation_sizes --> 0 <= index <= nmrs_.size().
    void* const allocation = index == nmrs_.size() ? nmrs_[0].mpp()->allocate() : nmrs_[index].allocate(allocation_size);
    if (allocation == nullptr)
      throw std::bad_alloc{};
    return static_cast<value_type*>(allocation);
  }

  static std::size_t optimal_capacity(std::size_t n)
  {
    // Do not call this function with an n larger than maximum_number_of_elements.
    ASSERT(n <= maximum_number_of_elements);
    std::size_t const allocation_size = elements_to_allocation_size(n);
    return allocation_size_to_elements(allocation_size);
  }

  std::allocation_result<pointer, size_type> allocate_at_least(std::size_t n)
  {
    if (AI_UNLIKELY(n > maximum_number_of_elements))
      return {allocate(n), n};
    std::size_t const count = optimal_capacity(n);
    return {allocate(count), count};
  }

  // Return storage at p to the shared size-class resource selected by the corresponding n-object allocation.
  void deallocate(value_type* p, std::size_t n) noexcept
  {
    // This allocator returns nullptr for zero-sized allocations, therefore we need to support deallocating that.
    if (AI_UNLIKELY(p == nullptr))
      return;
    if (AI_UNLIKELY(n > maximum_number_of_elements))
    {
      std::allocator<value_type>{}.deallocate(p, n);
      return;
    }
    std::size_t const allocation_size = elements_to_allocation_size(n);
#if CW_DEBUG
    std::size_t const count = allocation_size_to_elements(allocation_size);
    ASSERT(elements_to_allocation_size(count) == allocation_size);
#endif
    int const index = allocation_size_to_nmr_index(allocation_size);
    Dout(dc::memory, "Deallocating " << n << " elements from index " << index << " (" << (index == nmrs_.size() ? mpp_block_size : allocation_size) << " bytes).");
    if (AI_UNLIKELY(index == nmrs_.size()))
      nmrs_[0].mpp()->deallocate(p);
    else
      nmrs_[index].deallocate(p);
  }
};

} // namespace memory
