#pragma once

#include <cstddef>
#include <cstdint>

// Desktop handling of attribute words decoded with the public Xbox One ERA
// XALLOC_ATTRIBUTES layout (SlimEra era.h): objectType:14, pageSize:2, allocatorId:8,
// alignment:5, memoryType:4, reserved:31. That Scarlett uses this layout is an
// assumption; allocator-ID meaning and GPU visibility of graphics types are unconfirmed.
// Heap and graphics memory types are served. Physical memory types, undefined
// field values and alignments the desktop cannot honor return a null pointer.
namespace era_heap {
struct Fields {
    std::uint32_t object_type;
    std::uint32_t page_size;     // 0: 4 KB, 1: 64 KB, 2: 4 MB, 3: undefined
    std::uint32_t allocator_id;
    std::uint32_t alignment;     // log2 of the requested alignment
    std::uint32_t memory_type;   // 0: heap, 1-6: graphics, 7-9: physical
    std::uint64_t reserved;
};

Fields decode(std::uint64_t attributes) noexcept;
void* allocate(std::size_t bytes, std::uint64_t attributes) noexcept;
// Releases a heap-type allocation (memory type 0), as before.
void release(void* allocation) noexcept;
// Releases any allocation given the attributes used to allocate it, as XMemFree callers do.
void release_with_attributes(void* allocation, std::uint64_t attributes) noexcept;
}
