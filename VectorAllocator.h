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
using allocation_size_type = std::uint_fast32_t;

namespace detail {
static constexpr allocation_size_type default_alignment = sizeof(std::size_t);
static constexpr allocation_size_type default_smallest_allocation = default_alignment;

template <allocation_size_type mpp_block_size, allocation_size_type smallest_allocation>
struct MaxToLargest
{
  static constexpr int N = utils::log2(mpp_block_size / smallest_allocation);
  static_assert(N > 0, "smallest_allocation must fit at least twice inside mpp_block_size");
  // The demand is that largest_allocation = smallest_allocation * 2^N, where N is the largest possible integral value
  // such that mpp_block_size / largest_allocation >= 2.
  static constexpr allocation_size_type largest_allocation = (allocation_size_type{1} << (N - 1)) * smallest_allocation;
};
} // namespace detail

template <allocation_size_type smallest_allocation, allocation_size_type largest_allocation, allocation_size_type alignment>
class GeometricMemoryResource
{
 protected:
  using allocation_class_type = std::uint_least16_t;
  using nmr_index_type = int;                           // The type returned by utils::log2.

  // Sanity checks.
  static_assert(utils::is_power_of_two(alignment), "alignment must be a power of two");
  static_assert(alignment < 32, "GeometricMemoryResource is not suited for large alignments");
  static_assert(smallest_allocation >= sizeof(PtrTag) && smallest_allocation % alignof(PtrTag) == 0, "SimpleSegregatedStorage must be able to hold a PtrTag");
  static_assert(smallest_allocation <= largest_allocation);
  static_assert(smallest_allocation % alignment == 0, "smallest_allocation must be a multiple of alignment");

  // allocation_size must be smallest_allocation times a power of two.
  static constexpr nmr_index_type allocation_size_to_nmr_index(allocation_size_type allocation_size)
  {
    // Call this function only on values returned by elements_to_allocation_size(n), where n <= maximum_number_of_elements.
    ASSERT(smallest_allocation <= allocation_size);
    allocation_class_type const allocation_class = static_cast<allocation_class_type>(allocation_size / smallest_allocation);
    ASSERT(utils::is_power_of_two(allocation_class));
    return utils::log2(allocation_class);
  }

 protected:
  // nmrs_ contains the NodeMemoryResource's for the size classes {smallest_allocation, ..., largest_allocation}.
  static constexpr nmr_index_type number_of_allocation_sizes = allocation_size_to_nmr_index(largest_allocation) + 1;
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

// class VectorAllocator
//
// This allocator is intended to be used with std::vector.
//
// Allocation requests are rounded up to the next supported size class and served by the pool associated with that class.
// The smallest supported size class is given by `smallest_allocation`; each subsequent size class is twice as large.
// The largest supported size class is given by `largest_allocation`, defined by:
//
//     largest_allocation <= mpp_block_size / 2 < 2 * largest_allocation.
//
// Note that mpp_block_size will be even as it is a multiple of the size of a memory page.
//
// The existing size classes are then
//
//     {smallest_allocation, smallest_allocation * 2, smallest_allocation * 4, smallest_allocation * 8, ..., largest_allocation}
//
// Thus `largest_allocation` is `smallest_allocation` times some power of two.
//
// Because only a single block with size `2 * largest_allocation` fits in a memory pool block of size `mpp_block_size`,
// we can't use a NodeMemoryResource for that (which requires the memory pool block to be partitioned into at least two partitions).
// Therefore a memory pool block (of size `mpp_block_size`) is returned directly for sizes in the range (largest_allocation, mpp_block_size].
//
// An allocation request larger than mpp_block_size is served by std::allocator.
// Allocations of size 0 return nullptr. Deallocating nullptr is a no-op.
//
template <typename T, allocation_size_type smallest_allocation = detail::default_smallest_allocation,
          allocation_size_type mpp_block_size = memory::MemoryPagePool::default_block_size,
          allocation_size_type alignment = detail::default_alignment>
class VectorAllocator
    : public GeometricMemoryResource<smallest_allocation, detail::MaxToLargest<mpp_block_size, smallest_allocation>::largest_allocation, alignment>
{
  static_assert(alignof(T) <= alignment, "The used alignment must be enough for the stored element");

 public:
  static constexpr std::size_t element_size = sizeof(T);
  static constexpr allocation_size_type largest_allocation = detail::MaxToLargest<mpp_block_size, smallest_allocation>::largest_allocation;
  using Base_ = GeometricMemoryResource<smallest_allocation, largest_allocation, alignment>;

  static constexpr std::size_t allocation_size_to_elements(allocation_size_type size) { return size / element_size; }

  // And the inverse of that.
  static constexpr allocation_size_type elements_to_allocation_size(std::size_t elements)
  {
    // This function must return `allocation_size = smallest_allocation * 2^N` where N is the smallest non-negative integral
    // value such that `elements * element_size <= allocation_size`. Except when elements == 0 in which case it may return 0.
    std::size_t const bytes = elements * element_size;
    std::size_t const units = bytes / smallest_allocation + (bytes % smallest_allocation != 0);
    return smallest_allocation * utils::nearest_power_of_two(units);
  }

  // Trying to allocate more elements than this makes VectorAllocator fall back to std::allocator.
  // We use mpp_block_size here because that is the largest block returned.
  static constexpr std::size_t maximum_number_of_elements = allocation_size_to_elements(mpp_block_size);

  MemoryPagePool* mpp_;         // The MemoryPagePool used for allocation sizes in the range (largest_allocation, mpp_block_size].

 public:
  using value_type = T;
  using pointer = value_type*;
  using size_type = std::size_t;

  template <typename U>
  struct rebind
  {
    using other = VectorAllocator<U, smallest_allocation, mpp_block_size, alignment>;
  };

  VectorAllocator(memory::MemoryPagePool& mpp)
    : GeometricMemoryResource<smallest_allocation, largest_allocation, alignment>(mpp),
      mpp_(&mpp)
  {
    // mpp_block_size must be the same as the size that was passed to mpp.
    if (mpp_block_size != mpp.block_size())
      throw std::invalid_argument("VectorAllocator: MemoryPagePool block size does not match mpp_block_size");
  }

  template <typename U>
  VectorAllocator(VectorAllocator<U, smallest_allocation, mpp_block_size, alignment> const& other) noexcept : Base_(other), mpp_(other.mpp_) { }

 private:
  using typename Base_::allocation_class_type;
  using Base_::allocation_size_to_nmr_index;
  using Base_::nmrs_;

  // The largest value that we pass to allocation_size_to_nmr_index is `elements_to_allocation_size(maximum_number_of_elements)`.
  // Therefore that value divided by smallest_allocation must fit in an allocation_class_type; see allocation_size_to_nmr_index.
  static_assert(std::numeric_limits<allocation_class_type>::max() >= elements_to_allocation_size(maximum_number_of_elements) / smallest_allocation);

 public:
  // Allocate uninitialized storage for n value_type objects from the shared resource for its power-of-two size class.
  //
  // Throws std::bad_alloc when the upstream page pool cannot supply another block.
  value_type* allocate(std::size_t n)
  {
    // A zero-sized allocation must be supported by a conforming allocator. The result is unspecified; we choose to return nullptr.
    if (AI_UNLIKELY(n == 0))
      return nullptr;
    // This test makes sure that the requested allocation, n * element_size, fits in a single MemoryPagePool block.
    if (AI_UNLIKELY(n > maximum_number_of_elements))
      return std::allocator<value_type>{}.allocate(n);

    allocation_size_type const allocation_size = elements_to_allocation_size(n);
    int const index = allocation_size_to_nmr_index(allocation_size);
    Dout(dc::memory, "Allocating " << (index < nmrs_.size() ? allocation_size : mpp_block_size) << " bytes from index " << index << ".");
    void* const allocation = index < nmrs_.size() ? nmrs_[index].allocate(allocation_size) : mpp_->allocate();
    if (allocation == nullptr)
      throw std::bad_alloc{};
    return static_cast<value_type*>(allocation);
  }

  static std::size_t optimal_capacity(std::size_t n)
  {
    if (AI_UNLIKELY(n > maximum_number_of_elements))
      return n;
    std::size_t const allocation_size = elements_to_allocation_size(n);
    return allocation_size_to_elements(allocation_size);
  }

  std::allocation_result<pointer, size_type> allocate_at_least(std::size_t n)
  {
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
    int const index = allocation_size_to_nmr_index(allocation_size);
    Dout(dc::memory, "Deallocating " << n << " elements from index " << index << " (" << (index < nmrs_.size() ? allocation_size : mpp_block_size) << " bytes).");
    if (AI_LIKELY(index < nmrs_.size()))
      nmrs_[index].deallocate(p);
    else
      mpp_->deallocate(p);
  }

  template <typename U>
  bool operator==(VectorAllocator<U, smallest_allocation, mpp_block_size, alignment> const& other) const noexcept
  {
    return mpp_ == other.mpp_;
  }
};

} // namespace memory
