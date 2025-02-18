#include <algorithm_impl.hpp>
#include <exception.hpp>
#include <heap.hpp>
#include <irql.hpp>

namespace ktl {
namespace crt {
void initialize_heap() noexcept {
  ExInitializeDriverRuntime(DrvRtPoolNxOptIn);
}

static constexpr std::align_val_t get_max_alignment_for_pool(
    pool_type_t pool_type) {
  std::align_val_t max_alignment{};
  switch (pool_type) {
    case NonPagedPoolCacheAligned:
    case PagedPoolCacheAligned:
    case NonPagedPoolCacheAlignedMustS:
    case NonPagedPoolCacheAlignedSession:
    case PagedPoolCacheAlignedSession:
    case NonPagedPoolCacheAlignedMustSSession:
    case NonPagedPoolNxCacheAligned:
      max_alignment = CACHE_LINE_ALLOCATION_ALIGNMENT;
      break;
    default:
      max_alignment = DEFAULT_ALLOCATION_ALIGNMENT;
      break;
  }
  return max_alignment;
}

static void* allocate_impl(const alloc_request& request) noexcept {
  const auto [bytes_count, pool_type, alignment, pool_tag]{request};

  crt_assert_with_msg(pool_tag != 0, "pool tag must not be equal to zero");
  crt_assert_with_msg(
      get_current_irql() <= DISPATCH_LEVEL,
      "memory allocations are disabled at IRQL > DISPATCH_LEVEL due to usage "
      "of global executive spinlock to protect NT Virtual Memory Manager's PFN "
      "database");

  if (request.alignment <= get_max_alignment_for_pool(pool_type)) {
    return ExAllocatePoolWithTag(pool_type, bytes_count, pool_tag);
  }

  crt_assert_with_msg(alignment <= MAX_ALLOCATION_ALIGNMENT,
                      "allocation alignment is too large");

  const size_t page_aligned_size{
      (max)(bytes_count, static_cast<size_t>(MAX_ALLOCATION_ALIGNMENT))};
  return ExAllocatePoolWithTag(pool_type, page_aligned_size, pool_tag);
}

static void deallocate_impl(void* memory_block, pool_tag_t pool_tag) noexcept {
  crt_assert_with_msg(memory_block, "invalid memory block");
  crt_assert_with_msg(pool_tag != 0, "pool tag must not be equal to zero");
  ExFreePoolWithTag(memory_block, pool_tag);
}
}  // namespace crt

template <>
void* allocate_memory<OnAllocationFailure::DoNothing>(
    alloc_request request) noexcept {
  return crt::allocate_impl(request);
}

template <>
void* allocate_memory<OnAllocationFailure::ThrowException>(
    alloc_request request) {
  void* const memory{crt::allocate_impl(request)};
  if (!memory) {
    throw bad_alloc{};
  }
  return memory;
}

void deallocate_memory(free_request request) noexcept {
  if (auto* ptr = request.memory_block; ptr) {
    crt::deallocate_impl(ptr, request.pool_tag);
  }
}
}  // namespace ktl