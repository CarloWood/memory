#include "sys.h"
#include "SimpleSegregatedStorage.h"
#include "debug.h"

namespace memory {

bool SimpleSegregatedStorage::try_allocate_more(std::function<bool()> const& add_new_block)
{
  std::scoped_lock<std::mutex> lk(m_add_block_mutex);
  return m_head.load(std::memory_order_relaxed) != nullptr || add_new_block();
}

// Only call this from the lambda add_new_block that was passed to allocate.
void SimpleSegregatedStorage::add_block(void* block, size_t block_size, size_t partition_size)
{
  unsigned int const number_of_partitions = block_size / partition_size;

  // block_size must be a multiple of partition_size (at least 2 times).
  ASSERT(number_of_partitions > 1);

  char* const first_ptr = static_cast<char*>(block);
  char* const last_ptr = first_ptr + (number_of_partitions - 1) * partition_size;     // > first_ptr, see ASSERT.
  char* node = last_ptr;
  do
  {
    char* next_node = node;
    node = next_node - partition_size;
    reinterpret_cast<FreeNode*>(node)->m_next = reinterpret_cast<FreeNode*>(next_node);
  }
  while (node != first_ptr);
  FreeNode* first_node = reinterpret_cast<FreeNode*>(first_ptr);
  FreeNode* last_node = reinterpret_cast<FreeNode*>(last_ptr);
  last_node->m_next = m_head.load(std::memory_order_relaxed);
  while (!m_head.compare_exchange_weak(last_node->m_next, first_node, std::memory_order_release))
    ;
}

} // namespace memory
