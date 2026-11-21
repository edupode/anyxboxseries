// Copyright (c) 2026 edupode
// SPDX-License-Identifier: CPAL-1.0
//
// The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
// Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
// The Original Code is AnyXboxSeries.
// The Original Developer is the Initial Developer.
// The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
//

#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <bit>
#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace {
struct Graphics {
    decltype(&D3D12CreateRootSignatureDeserializer) deserialize;
    decltype(&D3D12SerializeVersionedRootSignature) serialize;
};

bool check_round_trip(const Graphics& graphics, const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& input) {
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    const HRESULT serialized = graphics.serialize(&input, blob.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(serialized) || !blob || blob->GetBufferSize() == 0) {
        std::fprintf(stderr, "Serialization version %d returned 0x%08lX\n",
                     input.Version, static_cast<unsigned long>(serialized));
        return false;
    }

    ComPtr<ID3D12RootSignatureDeserializer> deserializer;
    const HRESULT decoded = graphics.deserialize(blob->GetBufferPointer(), blob->GetBufferSize(),
                                                 IID_PPV_ARGS(deserializer.GetAddressOf()));
    if (FAILED(decoded) || !deserializer) {
        std::fprintf(stderr, "Deserialization version %d returned 0x%08lX\n",
                     input.Version, static_cast<unsigned long>(decoded));
        return false;
    }
    const auto* result = deserializer->GetRootSignatureDesc();
    if (!result || result->NumParameters != 1 ||
        result->Flags != D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT ||
        result->pParameters[0].ParameterType != D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS ||
        result->pParameters[0].Constants.Num32BitValues != 2 ||
        result->pParameters[0].Constants.ShaderRegister != 3 ||
        result->pParameters[0].Constants.RegisterSpace != 1) {
        std::fprintf(stderr, "Root signature data changed during version %d round-trip\n", input.Version);
        return false;
    }
    return true;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: wsx_root_signature_test <absolute DLL path>\n");
        return 2;
    }
    const HMODULE shim = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                                        LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!shim) {
        std::fprintf(stderr, "LoadLibraryExW returned error %lu\n", GetLastError());
        return 1;
    }
    const Graphics graphics{
        std::bit_cast<decltype(Graphics::deserialize)>(GetProcAddress(shim, "D3D12CreateRootSignatureDeserializer")),
        std::bit_cast<decltype(Graphics::serialize)>(GetProcAddress(shim, "D3D12SerializeVersionedRootSignature")),
    };
    if (!graphics.deserialize || !graphics.serialize) {
        std::fprintf(stderr, "Expected exports are missing\n");
        return 1;
    }

    D3D12_ROOT_PARAMETER original{};
    original.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    original.Constants = {3, 1, 2};
    original.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    constexpr auto standard_flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC description{};
    description.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
    description.Desc_1_0 = {1, &original, 0, nullptr, standard_flags};
    if (!check_round_trip(graphics, description)) {
        return 1;
    }

    D3D12_ROOT_PARAMETER1 versioned{};
    versioned.ParameterType = original.ParameterType;
    versioned.Constants = original.Constants;
    versioned.ShaderVisibility = original.ShaderVisibility;
    description.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    description.Desc_1_1 = {1, &versioned, 0, nullptr, standard_flags};
    if (!check_round_trip(graphics, description)) {
        return 1;
    }

    description.Desc_1_1.Flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(0x03000000);
    ComPtr<ID3DBlob> unsupported;
    if (graphics.serialize(&description, unsupported.GetAddressOf(), nullptr) != E_NOTIMPL || unsupported) {
        std::fprintf(stderr, "Xbox-only root flags must return E_NOTIMPL\n");
        return 1;
    }

    const std::array<unsigned char, 32> invalid_data{};
    ComPtr<ID3D12RootSignatureDeserializer> invalid;
    if (SUCCEEDED(graphics.deserialize(invalid_data.data(), invalid_data.size(),
                                       IID_PPV_ARGS(invalid.GetAddressOf()))) || invalid) {
        std::fprintf(stderr, "Invalid serialized signature was accepted\n");
        return 1;
    }

    std::puts("Root signature versions 1.0 and 1.1 round-tripped. Flag and blob error paths passed.");
    return 0;
}
