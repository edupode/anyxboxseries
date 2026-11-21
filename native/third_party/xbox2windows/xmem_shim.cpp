// Desktop xmem.dll exporting the four functions the package DLLs import by name.
// Call shapes come from the observed call sites, not from a published header:
//   XMemAlloc(size, attributes) and XMemFree(allocation, attributes): Bink and AMD callers.
//   XMemVirtualQuery(process, address, buffer): the four BVH callers pass no length, supply a
//     zeroed 64-byte buffer and test the 32-bit result.
//   XMemGetWorkingSetStatistics(selector, output): the seven AMD callers test the 32-bit
//     result and use zero when it fails.
#include "era_heap.h"
#include "era_query.h"

#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {
// The BVH callers' query buffer. era_query writes at most its first 0x31 bytes.
constexpr std::size_t query_buffer_bytes = 64;

bool is_graphics(std::uint64_t attributes) noexcept
{
    const auto type = era_heap::decode(attributes).memory_type;
    return type >= 1 && type <= 6;
}
}

extern "C" void* XMemAlloc(SIZE_T bytes, ULONGLONG attributes)
{
    void* allocation = era_heap::allocate(bytes, attributes);
    // Desktop policy: graphics memory types count as GPU-intended for XMemVirtualQuery.
    if (allocation && is_graphics(attributes) && !era_query::register_gpu_region(allocation, bytes)) {
        era_heap::release_with_attributes(allocation, attributes);
        return nullptr;
    }
    return allocation;
}

extern "C" void XMemFree(void* allocation, ULONGLONG attributes)
{
    if (allocation && is_graphics(attributes)) {
        era_query::unregister_gpu_region(allocation);
    }
    era_heap::release_with_attributes(allocation, attributes);
}

extern "C" BOOL XMemVirtualQuery(HANDLE process, LPCVOID address, void* buffer)
{
    return era_query::query(process, address, buffer, query_buffer_bytes) != 0;
}

// No public reference defines the selector or the output units, so report the call as
// unsupported. Every observed caller treats a failed call as contributing zero.
extern "C" BOOL XMemGetWorkingSetStatistics(UINT32 selector, void* statistics)
{
    UNREFERENCED_PARAMETER(selector);
    UNREFERENCED_PARAMETER(statistics);
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
