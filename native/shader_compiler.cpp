// Copyright (c) 2026 edupode
// SPDX-License-Identifier: CPAL-1.0
//
// The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
// Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
// The Original Code is AnyXboxSeries.
// The Original Developer is the Initial Developer.
// The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
//

#include "shader_compiler.h"

#include <Windows.h>
#include <oaidl.h>
#include <dxcapi.h>
#include <wrl/client.h>
#include <bit>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
using Microsoft::WRL::ComPtr;

class Module {
public:
    explicit Module(const std::filesystem::path& path)
        : handle_(LoadLibraryExW(path.c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if (!handle_) {
            const DWORD error = GetLastError();
            throw std::runtime_error("Could not load " + path.filename().string() +
                                     " (Win32 error " + std::to_string(error) + ").");
        }
    }

    ~Module() { FreeLibrary(handle_); }
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    FARPROC symbol(const char* name) const { return GetProcAddress(handle_, name); }

private:
    HMODULE handle_;
};

void check(HRESULT status, const char* operation) {
    if (FAILED(status)) {
        char code[11]{};
        std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(status));
        throw std::runtime_error(std::string(operation) + " failed (" + code + ").");
    }
}
}

std::vector<std::byte> compile_compute_shader(const std::filesystem::path& dxc_directory,
                                              std::string_view hlsl) {
    const auto folder = std::filesystem::absolute(dxc_directory);
    // Keep the matching signing library loaded until all DXC objects are released.
    const Module validator(folder / L"dxil.dll");
    const Module compiler_module(folder / L"dxcompiler.dll");
    const auto create = std::bit_cast<decltype(&DxcCreateInstance)>(
        compiler_module.symbol("DxcCreateInstance"));
    if (!create) {
        throw std::runtime_error("dxcompiler.dll does not export DxcCreateInstance.");
    }

    ComPtr<IDxcCompiler3> compiler;
    check(create(CLSID_DxcCompiler, IID_PPV_ARGS(compiler.GetAddressOf())), "DxcCreateInstance");
    const DxcBuffer source{hlsl.data(), hlsl.size(), DXC_CP_UTF8};
    const wchar_t* arguments[] = {L"-E", L"main", L"-T", L"cs_6_6", L"-HV", L"2021", L"-WX"};
    ComPtr<IDxcResult> result;
    check(compiler->Compile(&source, arguments, static_cast<UINT32>(std::size(arguments)), nullptr,
                            IID_PPV_ARGS(result.GetAddressOf())), "IDxcCompiler3::Compile");
    ComPtr<IDxcBlobUtf8> errors;
    if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(errors.GetAddressOf()), nullptr)) &&
        errors && errors->GetStringLength()) {
        std::fwrite(errors->GetStringPointer(), 1, errors->GetStringLength(), stderr);
    }
    HRESULT status{};
    check(result->GetStatus(&status), "IDxcResult::GetStatus");
    check(status, "HLSL compilation");
    ComPtr<IDxcBlob> object;
    check(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(object.GetAddressOf()), nullptr),
          "Read DXIL object");
    if (!object || !object->GetBufferSize()) {
        throw std::runtime_error("DXC returned an empty object.");
    }
    const auto* bytes = static_cast<const std::byte*>(object->GetBufferPointer());
    return {bytes, bytes + object->GetBufferSize()};
}
