// Copyright (c) 2026 edupode
// SPDX-License-Identifier: CPAL-1.0
//
// The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
// Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
// The Original Code is AnyXboxSeries.
// The Original Developer is the Initial Developer.
// The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
//

#include "boot_api.h"
#include "graphics_runtime.h"

#include <wrl/client.h>

#include <array>
#include <bit>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <string_view>
#include <type_traits>

namespace {
std::filesystem::path executable_directory() {
    std::array<wchar_t, 32768> path{};
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (size == 0 || size >= path.size()) {
        return {};
    }
    return std::filesystem::path(path.data()).parent_path();
}

int boot(const wchar_t* build_path, const wchar_t* selected_module, AdapterMode adapter_mode) {
    std::filesystem::path package;
    if (build_path) {
        package = std::filesystem::absolute(build_path);
        if (!std::filesystem::is_directory(package)) {
            std::fprintf(stderr, "Build directory unavailable: %ls\n", package.c_str());
            return 2;
        }
        std::printf("Build directory: %ls\n", package.c_str());
    }

    std::filesystem::path module_path;
    if (selected_module) {
        module_path = std::filesystem::absolute(selected_module);
    } else {
        const auto directory = executable_directory();
        if (directory.empty()) {
            std::fprintf(stderr, "Could not determine the bootstrap directory\n");
            return 3;
        }
        module_path = directory / L"wsx_boot_graphics.dll";
    }
    constexpr DWORD search_order = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32;
    const std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)> module(
        LoadLibraryExW(module_path.c_str(), nullptr, search_order), FreeLibrary);
    if (!module) {
        std::fprintf(stderr, "LoadLibraryExW returned Win32 error %lu: %ls\n",
                     GetLastError(), module_path.c_str());
        return 3;
    }
    const auto start = std::bit_cast<WsxStartV1Proc>(GetProcAddress(module.get(), wsx_start_symbol));
    if (!start) {
        std::fprintf(stderr, "%s entry missing: %ls\n", wsx_start_symbol, module_path.c_str());
        return 3;
    }

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if (FAILED(create_graphics_device(adapter_mode, device.GetAddressOf()))) {
        return 1;
    }
    std::printf("Calling %s in %ls\n", wsx_start_symbol, module_path.filename().c_str());
    std::fflush(stdout);
    const HRESULT status = start(device.Get(), package.empty() ? nullptr : package.c_str());
    std::printf("Module returned 0x%08lX\n", static_cast<unsigned long>(status));
    return FAILED(status) ? 1 : 0;
}
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* build_path = nullptr;
    const wchar_t* selected_module = nullptr;
    AdapterMode adapter_mode = AdapterMode::Preferred;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if (arg == L"--warp") {
            adapter_mode = AdapterMode::Warp;
        } else if (arg == L"--legacy-dxgi") {
            adapter_mode = AdapterMode::Standard;
        } else if (arg == L"--build" && i + 1 < argc) {
            build_path = argv[++i];
        } else if (arg == L"--module" && i + 1 < argc) {
            selected_module = argv[++i];
        } else {
            std::fprintf(stderr, "Usage: wsx_bootstrap [--build directory] [--module path.dll] [--warp | --legacy-dxgi]\n");
            return 2;
        }
    }
    try {
        return boot(build_path, selected_module, adapter_mode);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
