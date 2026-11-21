// Copyright (c) 2026 edupode
// SPDX-License-Identifier: CPAL-1.0
//
// The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License");
// you may not use this file except in compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/CPAL-1.0. Software distributed under the License is provided on an "AS IS"
// basis, without warranty. See the License for the specific language governing rights and limitations.
// The Original Code is AnyXboxSeries. The Initial Developer is edupode. All portions written by edupode are
// Copyright (c) 2026 edupode. All Rights Reserved.

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
using XMemAllocProc = void* (*)(SIZE_T, ULONGLONG);
using XMemFreeProc = void (*)(void*, ULONGLONG);
using XMemVirtualQueryProc = BOOL (*)(HANDLE, LPCVOID, void*);
using XMemGetWorkingSetStatisticsProc = BOOL (*)(UINT32, void*);

int fail(const char* message, DWORD error = ERROR_SUCCESS)
{
    std::fprintf(stderr, "xmem shim check failed: %s", message);
    if (error != ERROR_SUCCESS) {
        std::fprintf(stderr, " (Win32 error %lu)", error);
    }
    std::fputc('\n', stderr);
    return 1;
}
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "Usage: wsx_xmem_shim_test <path-to-xmem.dll>\n");
        return 2;
    }

    HMODULE module = LoadLibraryExW(argv[1], nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        return fail("could not load xmem.dll", GetLastError());
    }
    const auto close_module = [&] { FreeLibrary(module); };
    const auto allocate = reinterpret_cast<XMemAllocProc>(GetProcAddress(module, "XMemAlloc"));
    const auto release = reinterpret_cast<XMemFreeProc>(GetProcAddress(module, "XMemFree"));
    const auto query = reinterpret_cast<XMemVirtualQueryProc>(GetProcAddress(module, "XMemVirtualQuery"));
    const auto working_set = reinterpret_cast<XMemGetWorkingSetStatisticsProc>(
        GetProcAddress(module, "XMemGetWorkingSetStatistics"));
    if (!allocate || !release || !query || !working_set) {
        close_module();
        return fail("one or more package-imported exports are missing", GetLastError());
    }

    void* heap_block = allocate(256, 0);
    if (!heap_block) {
        close_module();
        return fail("heap allocation returned null");
    }
    if ((reinterpret_cast<std::uintptr_t>(heap_block) & 0xF) != 0) {
        release(heap_block, 0);
        close_module();
        return fail("heap allocation did not meet the 16-byte default alignment");
    }
    const auto* heap_bytes = static_cast<const unsigned char*>(heap_block);
    for (std::size_t i = 0; i < 256; ++i) {
        if (heap_bytes[i] != 0) {
            release(heap_block, 0);
            close_module();
            return fail("heap allocation was not zero-initialized");
        }
    }
    release(heap_block, 0);

    constexpr ULONGLONG graphics_attributes = ULONGLONG{1} << 29;
    void* graphics_block = allocate(256, graphics_attributes);
    if (!graphics_block) {
        close_module();
        return fail("graphics allocation returned null");
    }
    std::array<unsigned char, 64> query_buffer{};
    if (!query(GetCurrentProcess(), graphics_block, query_buffer.data())) {
        release(graphics_block, graphics_attributes);
        close_module();
        return fail("XMemVirtualQuery failed for a live allocation", GetLastError());
    }
    DWORD state = 0;
    std::memcpy(&state, query_buffer.data() + 0x20, sizeof(state));
    if (state != MEM_COMMIT) {
        release(graphics_block, graphics_attributes);
        close_module();
        return fail("XMemVirtualQuery did not report the committed allocation");
    }
    release(graphics_block, graphics_attributes);

    std::array<unsigned char, 16> statistics{};
    statistics.fill(0xA5);
    SetLastError(ERROR_SUCCESS);
    if (working_set(0, statistics.data()) || GetLastError() != ERROR_NOT_SUPPORTED ||
        statistics.front() != 0xA5) {
        close_module();
        return fail("working-set statistics did not return the documented unsupported result");
    }

    close_module();
    std::puts("xmem shim: 4 exports present; heap, graphics query, and unsupported statistics checks passed");
    return 0;
}
