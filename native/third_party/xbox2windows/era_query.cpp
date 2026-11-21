#include "era_query.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <mutex>
#include <new>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

static_assert(sizeof(MEMORY_BASIC_INFORMATION) == era_query::tail_offset);

namespace era_query {
namespace {
std::mutex regions_lock;
std::map<std::uintptr_t, std::uintptr_t> regions;  // begin -> end (exclusive)

bool is_current_process(HANDLE process) noexcept
{
    return process == GetCurrentProcess() || GetProcessId(process) == GetCurrentProcessId();
}

bool is_registered(std::uintptr_t address)
{
    std::lock_guard<std::mutex> guard(regions_lock);
    auto next = regions.upper_bound(address);
    if (next == regions.begin()) {
        return false;
    }
    return address < std::prev(next)->second;
}
}

bool register_gpu_region(const void* base, std::size_t size) noexcept
{
    const auto begin = reinterpret_cast<std::uintptr_t>(base);
    if (!base || size == 0 || begin + size < begin) {
        return false;
    }
    const auto end = begin + size;
    std::lock_guard<std::mutex> guard(regions_lock);
    auto next = regions.lower_bound(begin);
    if ((next != regions.end() && next->first < end) ||
        (next != regions.begin() && std::prev(next)->second > begin)) {
        return false;
    }
    try {
        regions.emplace_hint(next, begin, end);
    } catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

bool unregister_gpu_region(const void* base) noexcept
{
    std::lock_guard<std::mutex> guard(regions_lock);
    return regions.erase(reinterpret_cast<std::uintptr_t>(base)) == 1;
}

std::size_t query(void* process, const void* address, void* buffer, std::size_t length) noexcept
{
    if (length < sizeof(MEMORY_BASIC_INFORMATION)) {
        SetLastError(ERROR_BAD_LENGTH);
        return 0;
    }
    if (!buffer) {
        SetLastError(ERROR_NOACCESS);
        return 0;
    }
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQueryEx(process, address, &info, sizeof(info)) != sizeof(info)) {
        return 0;
    }
    std::memcpy(buffer, &info, sizeof(info));
    if (length > tail_offset) {
        // Desktop policy, not a Scarlett field: mark addresses in registered GPU-intended regions.
        const bool registered = is_current_process(process) &&
                                is_registered(reinterpret_cast<std::uintptr_t>(address));
        static_cast<unsigned char*>(buffer)[tail_offset] = registered ? tail_registered : 0;
    }
    return sizeof(info);
}
}
