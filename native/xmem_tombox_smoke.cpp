// SPDX-License-Identifier: CPAL-1.0
// Copyright (c) 2026 edupode

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
struct XMemBasicInformation {
    ULONG_PTR base;
    ULONG_PTR allocation_base;
    DWORD allocation_protect;
    DWORD reserved0;
    SIZE_T region_size;
    DWORD state;
    DWORD protect;
    DWORD kind;
    DWORD reserved1;
    ULONGLONG flags;
    DWORD tag;
    char tag_text[4];
};

using XMemAllocFn = void* (WINAPI*)(SIZE_T, ULONGLONG);
using XMemFreeFn = void (WINAPI*)(void*, ULONGLONG);
using XMemVirtualAllocFn = void* (WINAPI*)(void*, SIZE_T, DWORD, ULONGLONG, DWORD);
using XMemVirtualQueryFn = int (WINAPI*)(HANDLE, const void*, XMemBasicInformation*);

int fail(const char* message, DWORD error = ERROR_SUCCESS) noexcept {
    std::fprintf(stderr, "TomBox XMem candidate check failed: %s", message);
    if (error != ERROR_SUCCESS) {
        std::fprintf(stderr, " (error %lu)", static_cast<unsigned long>(error));
    }
    std::fputc('\n', stderr);
    return 1;
}

template <typename T>
T load(HMODULE module, const char* name) noexcept {
    return reinterpret_cast<T>(GetProcAddress(module, name));
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        return fail("usage: wsx_xmem_tombox_smoke <candidate-xmem.dll>");
    }

    const HMODULE module = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        return fail("could not load candidate DLL", GetLastError());
    }

    constexpr std::array required_exports{
        "XMemAlloc", "XMemFree", "XMemAllocDefault", "XMemFreeDefault",
        "XMemVirtualAlloc", "XMemVirtualQuery", "XMemGetWorkingSetStatistics",
        "XMemGetWriteWatch", "XMemResetWriteWatch", "XMemAllocatePhysicalPages",
        "XMemFreePhysicalPages", "XMemMapPhysicalPages", "XMemMapPhysicalPagesScatter",
        "XMemGetAllocationStatistics", "XMemGetFreePageStatistics",
        "XMemGetPhysicalMemoryLayout", "XMemSetOption", "XMemRegisterGraphicsMemoryConfiguration",
    };
    for (const char* name : required_exports) {
        if (!GetProcAddress(module, name)) {
            FreeLibrary(module);
            return fail(name);
        }
    }

    const auto allocate = load<XMemAllocFn>(module, "XMemAlloc");
    const auto release = load<XMemFreeFn>(module, "XMemFree");
    const auto query = load<XMemVirtualQueryFn>(module, "XMemVirtualQuery");
    const auto virtual_allocate = load<XMemVirtualAllocFn>(module, "XMemVirtualAlloc");
    if (!allocate || !release || !query || !virtual_allocate) {
        FreeLibrary(module);
        return fail("required XMem function pointer was null");
    }

    void* heap_allocation = allocate(4096, 0);
    if (!heap_allocation) {
        FreeLibrary(module);
        return fail("XMemAlloc returned null", GetLastError());
    }
    auto* bytes = static_cast<unsigned char*>(heap_allocation);
    for (std::size_t i = 0; i < 4096; ++i) {
        bytes[i] = static_cast<unsigned char>(i * 31u + 7u);
    }
    for (std::size_t i = 0; i < 4096; ++i) {
        if (bytes[i] != static_cast<unsigned char>(i * 31u + 7u)) {
            release(heap_allocation, 0);
            FreeLibrary(module);
            return fail("XMemAlloc block failed a write/read check");
        }
    }

    XMemBasicInformation memory{};
    if (query(GetCurrentProcess(), heap_allocation, &memory) != 1 || memory.state != MEM_COMMIT) {
        release(heap_allocation, 0);
        FreeLibrary(module);
        return fail("XMemVirtualQuery did not report the live allocation", GetLastError());
    }
    release(heap_allocation, 0);

    void* virtual_allocation = virtual_allocate(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, 0, PAGE_READWRITE);
    if (!virtual_allocation) {
        FreeLibrary(module);
        return fail("XMemVirtualAlloc returned null", GetLastError());
    }
    const bool freed = VirtualFree(virtual_allocation, 0, MEM_RELEASE) != FALSE;
    FreeLibrary(module);
    if (!freed) {
        return fail("Windows VirtualFree could not release XMemVirtualAlloc block", GetLastError());
    }

    std::printf("TomBox XMem candidate: %zu exports present; heap allocation, query, write/read, and virtual allocation passed\n",
                required_exports.size());
    return 0;
}
