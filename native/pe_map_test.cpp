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

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
using Resolver = void* (__cdecl*)(const char* dll, const char* name, std::uint32_t ordinal);
using MapImage = void* (__cdecl*)(const wchar_t*, Resolver, std::uint32_t*, char*, std::size_t);
using MapRun = int (__cdecl*)(void*, std::uint32_t*);
using MapRelease = int (__cdecl*)(void*);

int fail(const char* message, DWORD error = ERROR_SUCCESS)
{
    std::fprintf(stderr, "PE map check failed: %s", message);
    if (error != ERROR_SUCCESS) {
        std::fprintf(stderr, " (Win32 error %lu)", error);
    }
    std::fputc('\n', stderr);
    return 1;
}
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "Usage: wsx_pe_map_test <wsx_pe_map.dll> <fixture.exe>\n");
        return 2;
    }

    HMODULE mapper = LoadLibraryExW(argv[1], nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!mapper) {
        return fail("could not load the PE mapper", GetLastError());
    }
    const auto close_mapper = [&] { FreeLibrary(mapper); };
    const auto map_image = reinterpret_cast<MapImage>(GetProcAddress(mapper, "pe_map_image"));
    const auto map_run = reinterpret_cast<MapRun>(GetProcAddress(mapper, "pe_map_run"));
    const auto map_release = reinterpret_cast<MapRelease>(GetProcAddress(mapper, "pe_map_release"));
    if (!map_image || !map_run || !map_release) {
        close_mapper();
        return fail("one or more PE mapper exports are missing");
    }

    std::uint32_t status = 0xFFFFFFFF;
    char detail[256]{};
    void* image = map_image(argv[2], nullptr, &status, detail, sizeof(detail));
    if (!image) {
        close_mapper();
        std::fprintf(stderr, "PE map check failed: map returned %lu · %s\n", status, detail);
        return 1;
    }

    std::uint32_t guest_exit_code = 0;
    const bool ran = map_run(image, &guest_exit_code) != 0;
    const bool released = map_release(image) != 0;
    const bool rejected_second_release = map_release(image) == 0;
    close_mapper();
    if (!ran || guest_exit_code != 0x51A7 || !released || !rejected_second_release) {
        return fail("mapped EXE entry, return code, or release behavior did not match");
    }

    std::puts("PE map: mapped synthetic AMD64 EXE, ran its entry point, and released it once");
    return 0;
}
