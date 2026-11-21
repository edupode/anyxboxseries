// Copyright (c) 2026 edupode
// SPDX-License-Identifier: CPAL-1.0
//
// The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
// Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
// The Original Code is AnyXboxSeries.
// The Original Developer is the Initial Developer.
// The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
//

#include "graphics_runtime.h"

#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
constexpr UINT width = 160;
constexpr UINT height = 120;
constexpr DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
using Pixel = std::array<unsigned char, 4>;
constexpr Pixel background{12, 25, 42, 255};
constexpr Pixel foreground{75, 224, 172, 255};

struct Failure {
    const char* operation;
    HRESULT status;
};

HRESULT report_error(const Failure& error) noexcept {
    const HRESULT status = FAILED(error.status) ? error.status : E_FAIL;
    std::fprintf(stderr, "%s failed with 0x%08lX\n", error.operation,
                 static_cast<unsigned long>(status));
    return status;
}

HRESULT report_error(const std::exception& error) noexcept {
    std::fprintf(stderr, "%s\n", error.what());
    return E_FAIL;
}

void check(HRESULT status, const char* operation) {
    if (FAILED(status)) {
        throw Failure{operation, status};
    }
}

void check_windows(bool success, const char* operation) {
    if (!success) {
        throw Failure{operation, HRESULT_FROM_WIN32(GetLastError())};
    }
}

constexpr std::array<float, 4> normalized(Pixel pixel) {
    return {pixel[0] / 255.0f, pixel[1] / 255.0f, pixel[2] / 255.0f, pixel[3] / 255.0f};
}

struct AdapterDevice {
    ComPtr<ID3D12Device> device;
    DXGI_ADAPTER_DESC1 description{};
};

AdapterDevice open_device(AdapterMode mode) {
    const HMODULE desktop = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    check_windows(desktop != nullptr, "LoadLibraryExW d3d12.dll");
    const auto create_device = std::bit_cast<decltype(&D3D12CreateDevice)>(
        GetProcAddress(desktop, "D3D12CreateDevice"));
    check_windows(create_device != nullptr, "GetProcAddress D3D12CreateDevice");

    ComPtr<IDXGIFactory4> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())), "CreateDXGIFactory1");
    if (mode != AdapterMode::Warp) {
        ComPtr<IDXGIFactory6> preferred;
        if (mode == AdapterMode::Preferred) {
            factory.As(&preferred);
        }
        std::puts(preferred ? "DXGI enumeration: high performance preference."
                            : "DXGI enumeration: standard adapter order.");
        for (UINT index = 0; ; ++index) {
            ComPtr<IDXGIAdapter1> adapter;
            const HRESULT result = preferred
                ? preferred->EnumAdapterByGpuPreference(
                    index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter.GetAddressOf()))
                : factory->EnumAdapters1(index, adapter.GetAddressOf());
            if (result == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            check(result, "Enumerate hardware adapters");
            AdapterDevice candidate;
            check(adapter->GetDesc1(&candidate.description), "GetDesc1");
            if (candidate.description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                continue;
            }
            const HRESULT opened = create_device(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                                  IID_PPV_ARGS(candidate.device.GetAddressOf()));
            if (SUCCEEDED(opened)) {
                return candidate;
            }
            std::printf("Hardware adapter %ls returned 0x%08lX\n",
                        candidate.description.Description, static_cast<unsigned long>(opened));
        }
    }

    ComPtr<IDXGIAdapter1> warp;
    check(factory->EnumWarpAdapter(IID_PPV_ARGS(warp.GetAddressOf())), "EnumWarpAdapter");
    AdapterDevice result;
    check(warp->GetDesc1(&result.description), "GetDesc1 WARP");
    check(create_device(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(result.device.GetAddressOf())),
          "D3D12CreateDevice WARP");
    return result;
}

ComPtr<ID3D12RootSignature> make_root_signature(ID3D12Device* device) {
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants = {0, 0, 4};
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC description{};
    description.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
    description.Desc_1_0 = {1, &parameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> serialized;
    // Both of these functions are resolved through the executable's PE imports
    // for d3d12_xs.dll. D3D12CreateDevice comes from the desktop DLL.
    check(D3D12SerializeVersionedRootSignature(&description, serialized.GetAddressOf(), nullptr),
          "d3d12_xs.D3D12SerializeVersionedRootSignature");
    ComPtr<ID3D12RootSignatureDeserializer> decoded;
    check(D3D12CreateRootSignatureDeserializer(serialized->GetBufferPointer(),
        serialized->GetBufferSize(), IID_PPV_ARGS(decoded.GetAddressOf())),
        "d3d12_xs.D3D12CreateRootSignatureDeserializer");
    const D3D12_ROOT_SIGNATURE_DESC* decoded_description = decoded->GetRootSignatureDesc();
    if (!decoded_description || decoded_description->NumParameters != 1 ||
        decoded_description->pParameters[0].ParameterType != parameter.ParameterType ||
        decoded_description->pParameters[0].Constants.Num32BitValues != 4) {
        throw Failure{"Root signature read-back", E_FAIL};
    }

    ComPtr<ID3D12RootSignature> root;
    check(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                     IID_PPV_ARGS(root.GetAddressOf())), "CreateRootSignature");
    return root;
}

ComPtr<ID3DBlob> compile_shader(const char* entry, const char* profile) {
    constexpr std::string_view shader = R"(
cbuffer DrawColor : register(b0) { float4 color; }

float4 Vertex(uint index : SV_VertexID) : SV_POSITION {
    const float2 points[3] = {float2(0.0, 0.75), float2(-0.75, -0.65), float2(0.75, -0.65)};
    return float4(points[index], 0.0, 1.0);
}

float4 PixelColor() : SV_TARGET { return color; }
)";
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> error;
    const HRESULT status = D3DCompile(shader.data(), shader.size(), "graphics_probe.hlsl",
        nullptr, nullptr, entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        bytecode.GetAddressOf(), error.GetAddressOf());
    if (FAILED(status) && error) {
        std::fwrite(error->GetBufferPointer(), 1, error->GetBufferSize(), stderr);
    }
    check(status, entry);
    return bytecode;
}

ComPtr<ID3D12PipelineState> make_pipeline(ID3D12Device* device, ID3D12RootSignature* root) {
    const auto vertex = compile_shader("Vertex", "vs_5_0");
    const auto pixel = compile_shader("PixelColor", "ps_5_0");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = root;
    pipeline.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
    pipeline.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    pipeline.BlendState.RenderTarget[0] = {FALSE, FALSE, D3D12_BLEND_ONE, D3D12_BLEND_ZERO,
        D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL};
    pipeline.SampleMask = UINT_MAX;
    pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline.RasterizerState.DepthClipEnable = TRUE;
    pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pipeline.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
        D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
    pipeline.DepthStencilState.BackFace = pipeline.DepthStencilState.FrontFace;
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = format;
    pipeline.SampleDesc = {1, 0};
    ComPtr<ID3D12PipelineState> result;
    check(device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(result.GetAddressOf())),
          "CreateGraphicsPipelineState");
    return result;
}

ComPtr<ID3D12Resource> make_resource(ID3D12Device* device, D3D12_HEAP_TYPE memory,
    const D3D12_RESOURCE_DESC& description, D3D12_RESOURCE_STATES state,
    const D3D12_CLEAR_VALUE* clear_value, const char* operation) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = memory;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    ComPtr<ID3D12Resource> resource;
    check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
        state, clear_value, IID_PPV_ARGS(resource.GetAddressOf())), operation);
    return resource;
}

void await_frame(ID3D12Device* device, ID3D12CommandQueue* queue) {
    ComPtr<ID3D12Fence> fence;
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())), "CreateFence");
    const std::unique_ptr<void, decltype(&CloseHandle)> event(
        CreateEventW(nullptr, FALSE, FALSE, nullptr), CloseHandle);
    check_windows(event != nullptr, "CreateEventW");
    check(queue->Signal(fence.Get(), 1), "Queue Signal");
    check(fence->SetEventOnCompletion(1, event.get()), "SetEventOnCompletion");
    const DWORD wait = WaitForSingleObject(event.get(), 15000);
    if (wait == WAIT_TIMEOUT) {
        throw Failure{"Wait for graphics queue", HRESULT_FROM_WIN32(WAIT_TIMEOUT)};
    }
    check_windows(wait == WAIT_OBJECT_0, "WaitForSingleObject fence");
    check(device->GetDeviceRemovedReason(), "GetDeviceRemovedReason");
}

std::vector<Pixel> draw_frame(ID3D12Device* device, ID3D12RootSignature* root, ID3D12PipelineState* pipeline) {
    constexpr auto clear_color = normalized(background);
    constexpr auto draw_color = normalized(foreground);
    D3D12_RESOURCE_DESC texture{};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = width;
    texture.Height = height;
    texture.DepthOrArraySize = 1;
    texture.MipLevels = 1;
    texture.Format = format;
    texture.SampleDesc = {1, 0};
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = format;
    std::memcpy(clear.Color, clear_color.data(), sizeof(clear.Color));
    const auto target = make_resource(device, D3D12_HEAP_TYPE_DEFAULT, texture,
        D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, "Create render target");

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT64 bytes = 0;
    device->GetCopyableFootprints(&texture, 0, 1, 0, &layout, nullptr, nullptr, &bytes);
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = bytes;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc = {1, 0};
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const auto readback = make_resource(device, D3D12_HEAP_TYPE_READBACK, buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, "Create readback buffer");
    std::printf("Memory ready. Readback size %llu bytes, row pitch %u bytes\n",
                static_cast<unsigned long long>(bytes), layout.Footprint.RowPitch);

    D3D12_DESCRIPTOR_HEAP_DESC descriptors{};
    descriptors.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    descriptors.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> views;
    check(device->CreateDescriptorHeap(&descriptors, IID_PPV_ARGS(views.GetAddressOf())),
          "CreateDescriptorHeap");
    const auto view = views->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(target.Get(), nullptr, view);

    D3D12_COMMAND_QUEUE_DESC queue_description{};
    queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    check(device->CreateCommandQueue(&queue_description, IID_PPV_ARGS(queue.GetAddressOf())), "CreateCommandQueue");
    ComPtr<ID3D12CommandAllocator> allocator;
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
          IID_PPV_ARGS(allocator.GetAddressOf())), "CreateCommandAllocator");
    ComPtr<ID3D12GraphicsCommandList> commands;
    check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
          pipeline, IID_PPV_ARGS(commands.GetAddressOf())), "CreateCommandList");

    const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    const D3D12_RECT scissor{0, 0, width, height};
    commands->RSSetViewports(1, &viewport);
    commands->RSSetScissorRects(1, &scissor);
    commands->OMSetRenderTargets(1, &view, FALSE, nullptr);
    commands->ClearRenderTargetView(view, clear_color.data(), 0, nullptr);
    commands->SetGraphicsRootSignature(root);
    commands->SetGraphicsRoot32BitConstants(0, 4, draw_color.data(), 0);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->DrawInstanced(3, 1, 0, 0);

    D3D12_RESOURCE_BARRIER transition{};
    transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Transition = {target.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
    commands->ResourceBarrier(1, &transition);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = target.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = layout;
    commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    check(commands->Close(), "Close command list");
    ID3D12CommandList* lists[]{commands.Get()};
    queue->ExecuteCommandLists(1, lists);
    await_frame(device, queue.Get());
    std::puts("Command queue fence completed.");

    std::vector<Pixel> pixels(width * height);
    const D3D12_RANGE read_range{0, static_cast<SIZE_T>(bytes)};
    void* mapped = nullptr;
    check(readback->Map(0, &read_range, &mapped), "Map readback");
    const auto* first_row = static_cast<const unsigned char*>(mapped) + layout.Offset;
    for (UINT y = 0; y < height; ++y) {
        std::memcpy(pixels.data() + y * width, first_row + y * layout.Footprint.RowPitch, width * sizeof(Pixel));
    }
    const D3D12_RANGE no_writes{0, 0};
    readback->Unmap(0, &no_writes);
    return pixels;
}

void verify_frame(const std::vector<Pixel>& pixels) {
    std::size_t filled = 0;
    for (const auto& pixel : pixels) {
        if (pixel == foreground) {
            ++filled;
        } else if (pixel != background) {
            throw Failure{"Unexpected pixel color", E_FAIL};
        }
    }
    if (pixels.front() != background || pixels[(height / 2) * width + width / 2] != foreground ||
        filled < 3500 || filled > 6500) {
        throw Failure{"Readback geometry check", E_FAIL};
    }
    std::printf("Frame verified: %u x %u, %zu triangle pixels, %zu background pixels\n",
                width, height, filled, pixels.size() - filled);
}

void save_frame(const std::filesystem::path& path, const std::vector<Pixel>& pixels) {
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto& pixel : pixels) {
        output.write(reinterpret_cast<const char*>(pixel.data()), 3);
    }
    output.close();
    if (!output) {
        throw Failure{"Save PPM frame", E_FAIL};
    }
    std::puts("Frame saved as PPM.");
}
}

HRESULT create_graphics_device(AdapterMode mode, ID3D12Device** device) noexcept {
    if (!device) {
        return E_POINTER;
    }
    *device = nullptr;
    try {
        auto runtime = open_device(mode);
        const bool software = (runtime.description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        std::printf("Direct3D 12 initialized: %ls (%s)\n", runtime.description.Description,
                    software ? "WARP" : "hardware");
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 features{};
        if (SUCCEEDED(runtime.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features, sizeof(features)))) {
            const auto tier = static_cast<UINT>(features.RaytracingTier);
            std::printf("DXR tier reported by the device: %u.%u\n", tier / 10, tier % 10);
        }
        *device = runtime.device.Detach();
        return S_OK;
    } catch (const Failure& error) {
        return report_error(error);
    } catch (const std::exception& error) {
        return report_error(error);
    }
}

HRESULT verify_graphics_frame(ID3D12Device* device, const wchar_t* frame_path) noexcept {
    if (!device) {
        return E_POINTER;
    }
    try {
        const auto root = make_root_signature(device);
        std::puts("Windows loader and both d3d12_xs root signature imports verified.");
        const auto pipeline = make_pipeline(device, root.Get());
        std::puts("Vertex shader, pixel shader and pipeline state ready.");
        const auto pixels = draw_frame(device, root.Get(), pipeline.Get());
        verify_frame(pixels);
        if (frame_path) {
            save_frame(frame_path, pixels);
        }
        return S_OK;
    } catch (const Failure& error) {
        return report_error(error);
    } catch (const std::exception& error) {
        return report_error(error);
    }
}
