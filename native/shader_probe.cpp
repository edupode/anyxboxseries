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
#include "shader_compiler.h"

#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
constexpr std::uint32_t readback_tag = 0x57535831;
constexpr unsigned unsupported = 77;
using ProbeWords = std::array<std::uint32_t, 2>;
using WinHandle = std::unique_ptr<void, decltype(&CloseHandle)>;

constexpr char compute_hlsl[] = R"(
cbuffer InitialRegisters : register(b0) { uint s0; uint s1; };
RWByteAddressBuffer result : register(u0);
[numthreads(1, 1, 1)]
void main() { result.Store2(0, uint2(s0 * s1 + 7u, 0x57535831u)); }
)";

constexpr char ray_hlsl[] = R"(
RaytracingAccelerationStructure scene : register(t0);
RWByteAddressBuffer result : register(u0);

uint trace(float x) {
    RayDesc ray;
    ray.Origin = float3(x, 0, -2);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = 0;
    ray.TMax = 10;
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, 0xff, ray);
    while (query.Proceed()) {}
    if (query.CommittedStatus() == COMMITTED_NOTHING) {
        return 0u;
    }
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT &&
        query.CommittedPrimitiveIndex() == 0u && abs(query.CommittedRayT() - 3.0) < 0.01) {
        return query.CommittedInstanceID();
    }
    return 0xffffffffu;
}

[numthreads(1, 1, 1)]
void main() {
    uint hit = trace(2);
    uint miss = trace(0);
    result.Store2(0, uint2(miss == 0u ? hit : 0xffffffffu, 0x57535831u));
}
)";

void check(HRESULT status, const char* operation) {
    if (FAILED(status)) {
        char code[11]{};
        std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(status));
        throw std::runtime_error(std::string(operation) + " failed (" + code + ").");
    }
}

struct Options {
    AdapterMode adapter = AdapterMode::Preferred;
    bool debug = false;
    bool caps_only = false;
    bool ray = false;
    std::filesystem::path dxc;
    std::filesystem::path hlsl;
    std::filesystem::path output;
    ProbeWords inputs{7, 5};
    std::optional<std::uint32_t> expected;
};

std::uint32_t parse_word(std::wstring_view arg) {
    int base = 10;
    if (arg.starts_with(L"0x") || arg.starts_with(L"0X")) {
        base = 16;
        arg.remove_prefix(2);
    }
    if (arg.empty() || arg.size() > 10) {
        throw std::invalid_argument("Expected an unsigned 32-bit integer.");
    }
    std::string ascii;
    for (wchar_t character : arg) {
        if (character > 127) {
            throw std::invalid_argument("Expected an unsigned 32-bit integer.");
        }
        ascii += static_cast<char>(character);
    }
    std::uint32_t value{};
    const char* end = ascii.data() + ascii.size();
    const auto [last, error] = std::from_chars(ascii.data(), end, value, base);
    if (error != std::errc{} || last != end) {
        throw std::invalid_argument("Expected an unsigned 32-bit integer.");
    }
    return value;
}

Options parse_args(int argc, wchar_t** argv) {
    Options options;
    bool custom_inputs = false;
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view arg(argv[index]);
        auto next = [&]() -> const wchar_t* {
            if (index + 1 == argc) {
                throw std::invalid_argument("Missing option value.");
            }
            return argv[++index];
        };
        if (arg == L"--warp") {
            options.adapter = AdapterMode::Warp;
        } else if (arg == L"--debug") {
            options.debug = true;
        } else if (arg == L"--caps") {
            options.caps_only = true;
        } else if (arg == L"--ray-query") {
            options.ray = true;
        } else if (arg == L"--dxc-dir") {
            options.dxc = next();
        } else if (arg == L"--compute") {
            options.hlsl = next();
        } else if (arg == L"--inputs") {
            options.inputs[0] = parse_word(next());
            options.inputs[1] = parse_word(next());
            custom_inputs = true;
        } else if (arg == L"--expect") {
            options.expected = parse_word(next());
        } else if (arg == L"--dxil") {
            options.output = next();
        } else {
            throw std::invalid_argument("Unrecognized option.");
        }
    }
    const bool custom = !options.hlsl.empty();
    if ((!options.caps_only && options.dxc.empty()) ||
        (custom != options.expected.has_value()) || (custom_inputs && !custom) ||
        (options.ray && custom) ||
        (options.caps_only && (options.ray || custom || !options.output.empty()))) {
        throw std::invalid_argument("Incompatible or incomplete options.");
    }
    return options;
}

void enable_debug_layer() {
    // The runtime must outlive the debug layer and any D3D12 objects.
    static const HMODULE desktop =
        LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!desktop) {
        throw std::runtime_error("Could not load the desktop Direct3D 12 runtime.");
    }
    const auto get_debug = std::bit_cast<decltype(&D3D12GetDebugInterface)>(
        GetProcAddress(desktop, "D3D12GetDebugInterface"));
    if (!get_debug) {
        throw std::runtime_error("Direct3D 12 debug interface is unavailable.");
    }
    ComPtr<ID3D12Debug> debug;
    check(get_debug(IID_PPV_ARGS(debug.GetAddressOf())), "Enable D3D12 debug layer");
    debug->EnableDebugLayer();
}

void verify_debug_messages(ID3D12Device& device) {
    ComPtr<ID3D12InfoQueue> info;
    check(device.QueryInterface(IID_PPV_ARGS(info.GetAddressOf())), "ID3D12InfoQueue");
    unsigned errors = 0;
    for (UINT64 index = 0; index < info->GetNumStoredMessages(); ++index) {
        SIZE_T bytes = 0;
        check(info->GetMessage(index, nullptr, &bytes), "D3D12 debug message size");
        std::vector<std::byte> storage(bytes);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        check(info->GetMessage(index, message, &bytes), "D3D12 debug message");
        if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
            message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
            ++errors;
            std::fprintf(stderr, "D3D12: %s\n", message->pDescription);
        }
    }
    std::printf("D3D12 debug layer errors: %u\n", errors);
    if (errors != 0) {
        throw std::runtime_error("The D3D12 debug layer reported errors.");
    }
}

ComPtr<ID3D12Resource> buffer(ID3D12Device& device, UINT64 size,
                              D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state,
                              D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    ComPtr<ID3D12Resource> result;
    check(device.CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                        IID_PPV_ARGS(result.GetAddressOf())),
          "Create probe buffer");
    return result;
}

ComPtr<ID3D12Resource> upload(ID3D12Device& device, std::span<const std::byte> data) {
    auto result = buffer(device, data.size(), D3D12_HEAP_TYPE_UPLOAD,
                         D3D12_RESOURCE_STATE_GENERIC_READ);
    const D3D12_RANGE no_reads{0, 0};
    void* mapped = nullptr;
    check(result->Map(0, &no_reads, &mapped), "Map upload buffer");
    std::memcpy(mapped, data.data(), data.size());
    const D3D12_RANGE written{0, data.size()};
    result->Unmap(0, &written);
    return result;
}

void transition(ID3D12GraphicsCommandList& list, ID3D12Resource& resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {&resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list.ResourceBarrier(1, &barrier);
}

void order_uav(ID3D12GraphicsCommandList& list, ID3D12Resource& resource) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = &resource;
    list.ResourceBarrier(1, &barrier);
}

struct BuildBuffers {
    ComPtr<ID3D12Resource> result;
    ComPtr<ID3D12Resource> scratch;
};

BuildBuffers build_structure(ID3D12Device5& device, ID3D12GraphicsCommandList4& list,
                             const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs) {
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes{};
    device.GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &sizes);
    if (!sizes.ResultDataMaxSizeInBytes || !sizes.ScratchDataSizeInBytes) {
        throw std::runtime_error("The driver returned invalid acceleration structure sizes.");
    }
    auto result = buffer(device, sizes.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                         D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto scratch = buffer(device, sizes.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
    desc.Inputs = inputs;
    desc.DestAccelerationStructureData = result->GetGPUVirtualAddress();
    desc.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
    list.BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
    order_uav(list, *result.Get());
    return {std::move(result), std::move(scratch)};
}

struct RayScene {
    ComPtr<ID3D12Resource> vertices;
    BuildBuffers bottom;
    ComPtr<ID3D12Resource> clone;
    ComPtr<ID3D12Resource> instance;
    BuildBuffers top;
};

RayScene build_ray_scene(ID3D12Device& device, ID3D12GraphicsCommandList& list) {
    ComPtr<ID3D12Device5> ray_device;
    ComPtr<ID3D12GraphicsCommandList4> ray_list;
    check(device.QueryInterface(IID_PPV_ARGS(ray_device.GetAddressOf())), "ID3D12Device5");
    check(list.QueryInterface(IID_PPV_ARGS(ray_list.GetAddressOf())), "ID3D12GraphicsCommandList4");
    const std::array vertices{-1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f};
    RayScene scene;
    scene.vertices = upload(device, std::as_bytes(std::span{vertices}));

    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexCount = 3;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.VertexBuffer = {scene.vertices->GetGPUVirtualAddress(), 3 * sizeof(float)};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &geometry;
    scene.bottom = build_structure(*ray_device.Get(), *ray_list.Get(), inputs);

    scene.clone = buffer(device, scene.bottom.result->GetDesc().Width, D3D12_HEAP_TYPE_DEFAULT,
                         D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ray_list->CopyRaytracingAccelerationStructure(scene.clone->GetGPUVirtualAddress(),
        scene.bottom.result->GetGPUVirtualAddress(), D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE);
    order_uav(list, *scene.clone.Get());

    D3D12_RAYTRACING_INSTANCE_DESC instance{};
    instance.Transform[0][0] = instance.Transform[1][1] = instance.Transform[2][2] = 1.0f;
    instance.Transform[0][3] = 2.0f;
    instance.InstanceID = 79;
    instance.InstanceMask = 0xff;
    instance.AccelerationStructure = scene.clone->GetGPUVirtualAddress();
    scene.instance = upload(device, std::as_bytes(std::span{&instance, 1}));

    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.InstanceDescs = scene.instance->GetGPUVirtualAddress();
    scene.top = build_structure(*ray_device.Get(), *ray_list.Get(), inputs);
    return scene;
}

ComPtr<ID3D12RootSignature> make_root_signature(ID3D12Device& device, bool ray) {
    D3D12_DESCRIPTOR_RANGE output{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable = {1, &output};
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants = {0, 0, 2};
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[2].Descriptor = {0, 0};
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
    desc.Desc_1_0 = {ray ? 3u : 2u, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> serialized;
    check(D3D12SerializeVersionedRootSignature(&desc, serialized.GetAddressOf(), nullptr),
          "Serialize probe root signature");
    ComPtr<ID3D12RootSignature> root;
    check(device.CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                     IID_PPV_ARGS(root.GetAddressOf())), "Create probe root signature");
    return root;
}

void submit_and_wait(ID3D12Device& device, ID3D12CommandQueue& queue, ID3D12GraphicsCommandList& list) {
    ComPtr<ID3D12Fence> fence;
    check(device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())),
          "Create readback fence");
    const WinHandle event(CreateEventW(nullptr, FALSE, FALSE, nullptr), CloseHandle);
    if (!event) {
        check(HRESULT_FROM_WIN32(GetLastError()), "Create readback event");
    }
    check(list.Close(), "Close shader command list");
    ID3D12CommandList* lists[] = {&list};
    queue.ExecuteCommandLists(1, lists);
    check(queue.Signal(fence.Get(), 1), "Signal readback fence");
    check(fence->SetEventOnCompletion(1, event.get()), "Arm readback fence");
    switch (WaitForSingleObject(event.get(), 15000)) {
    case WAIT_OBJECT_0:
        check(device.GetDeviceRemovedReason(), "Execute shader");
        break;
    case WAIT_TIMEOUT:
        throw std::runtime_error("Timed out waiting for the shader readback fence.");
    default:
        check(HRESULT_FROM_WIN32(GetLastError()), "Wait for shader readback");
        break;
    }
}

ProbeWords dispatch(ID3D12Device& device, std::span<const std::byte> dxil, const Options& options) {
    auto root = make_root_signature(device, options.ray);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = root.Get();
    pipeline.CS = {dxil.data(), dxil.size()};
    ComPtr<ID3D12PipelineState> pso;
    check(device.CreateComputePipelineState(&pipeline, IID_PPV_ARGS(pso.GetAddressOf())),
          "Create cs_6_6 pipeline");

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    check(device.CreateCommandQueue(&queue_desc, IID_PPV_ARGS(queue.GetAddressOf())), "Create probe queue");
    check(device.CreateCommandAllocator(queue_desc.Type, IID_PPV_ARGS(allocator.GetAddressOf())),
          "Create probe allocator");
    check(device.CreateCommandList(0, queue_desc.Type, allocator.Get(), pso.Get(),
                                   IID_PPV_ARGS(list.GetAddressOf())), "Create probe command list");

    const ProbeWords zero{};
    auto initial = upload(device, std::as_bytes(std::span{zero}));
    auto output = buffer(device, sizeof(zero), D3D12_HEAP_TYPE_DEFAULT,
                         D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto readback = buffer(device, sizeof(zero), D3D12_HEAP_TYPE_READBACK,
                           D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyBufferRegion(output.Get(), 0, initial.Get(), 0, sizeof(zero));
    transition(*list.Get(), *output.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    const D3D12_DESCRIPTOR_HEAP_DESC heap_desc{
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    ComPtr<ID3D12DescriptorHeap> heap;
    check(device.CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(heap.GetAddressOf())), "Create probe UAV heap");
    D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_R32_TYPELESS;
    view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    view.Buffer.NumElements = static_cast<UINT>(zero.size());
    view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device.CreateUnorderedAccessView(output.Get(), nullptr, &view, heap->GetCPUDescriptorHandleForHeapStart());

    RayScene scene;
    if (options.ray) {
        scene = build_ray_scene(device, *list.Get());
    }
    list->SetComputeRootSignature(root.Get());
    ID3D12DescriptorHeap* heaps[] = {heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
    list->SetComputeRoot32BitConstants(1, static_cast<UINT>(options.inputs.size()), options.inputs.data(), 0);
    if (options.ray) {
        list->SetComputeRootShaderResourceView(2, scene.top.result->GetGPUVirtualAddress());
    }
    list->Dispatch(1, 1, 1);
    transition(*list.Get(), *output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(readback.Get(), 0, output.Get(), 0, sizeof(zero));
    submit_and_wait(device, *queue.Get(), *list.Get());

    ProbeWords words{};
    const D3D12_RANGE read{0, sizeof(words)};
    void* mapped = nullptr;
    check(readback->Map(0, &read, &mapped), "Map shader readback");
    std::memcpy(words.data(), mapped, sizeof(words));
    const D3D12_RANGE no_writes{0, 0};
    readback->Unmap(0, &no_writes);
    return words;
}

std::string read_hlsl(const std::filesystem::path& path) {
    constexpr std::size_t limit = 1024 * 1024;
    std::ifstream file(path, std::ios::binary);
    std::string source(limit + 1, '\0');
    if (!file || !file.read(source.data(), static_cast<std::streamsize>(source.size())).eof() ||
        file.bad() || file.gcount() == 0) {
        throw std::runtime_error("Expected a readable, nonempty HLSL file (maximum 1 MiB).");
    }
    source.resize(static_cast<std::size_t>(file.gcount()));
    return source;
}

void write_dxil(const std::filesystem::path& path, std::span<const std::byte> dxil) {
    if (dxil.size() > MAXDWORD) {
        throw std::runtime_error("DXIL object size exceeds the write limit.");
    }
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        check(HRESULT_FROM_WIN32(GetLastError()), "Create new DXIL output file");
    }
    const WinHandle output(handle, CloseHandle);
    const auto size = static_cast<DWORD>(dxil.size());
    DWORD written{};
    if (!WriteFile(output.get(), dxil.data(), size, &written, nullptr)) {
        check(HRESULT_FROM_WIN32(GetLastError()), "Write DXIL output");
    }
    if (written != size) {
        throw std::runtime_error("Incomplete DXIL output.");
    }
}

unsigned run(const Options& options) {
    std::puts("AnyXboxSeries by edupode");
    if (options.debug) {
        enable_debug_layer();
    }
    ComPtr<ID3D12Device> device;
    check(create_graphics_device(options.adapter, device.GetAddressOf()), "Open graphics device");
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_6};
    const bool sm66 = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,
        &shader_model, sizeof(shader_model))) && shader_model.HighestShaderModel >= D3D_SHADER_MODEL_6_6;
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 ray{};
    const bool has_ray_data = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
        &ray, sizeof(ray)));
    const auto ray_tier = has_ray_data ? static_cast<unsigned>(ray.RaytracingTier) : 0;
    std::printf("Shader Model 6.6: %s\nDXR tier: %u.%u\n", sm66 ? "supported" : "unavailable",
                ray_tier / 10, ray_tier % 10);
    D3D12_FEATURE_DATA_ARCHITECTURE1 architecture{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &architecture, sizeof(architecture)))) {
        std::printf("UMA: %s, cache coherent: %s\n", architecture.UMA ? "yes" : "no",
                    architecture.CacheCoherentUMA ? "yes" : "no");
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1))) &&
        options1.WaveOps) {
        std::printf("Wave lanes: %u to %u\n", options1.WaveLaneCountMin, options1.WaveLaneCountMax);
    }
    if (options.caps_only) {
        if (options.debug) {
            verify_debug_messages(*device.Get());
        }
        return 0;
    }
    if (!sm66 || (options.ray && ray.RaytracingTier < D3D12_RAYTRACING_TIER_1_1)) {
        std::puts("Required native shader features are unavailable on this adapter.");
        return unsupported;
    }

    const std::string source = options.hlsl.empty()
        ? (options.ray ? ray_hlsl : compute_hlsl)
        : read_hlsl(options.hlsl);
    const auto dxil = compile_compute_shader(options.dxc, source);
    const auto words = dispatch(*device.Get(), dxil, options);
    const auto expected = options.expected.value_or(options.ray ? 79u : 42u);
    std::printf("cs_6_6: %zu bytes DXIL, GPU readback 0x%08X (expected 0x%08X), tag 0x%08X\n",
                dxil.size(), words[0], expected, words[1]);
    if (options.debug) {
        verify_debug_messages(*device.Get());
    }
    if (words[0] != expected || words[1] != readback_tag) {
        throw std::runtime_error("Shader output differed from the expected value or readback tag.");
    }
    if (options.ray) {
        std::puts("BLAS built and cloned, TLAS built, inline RayQuery hit and miss verified.");
    }
    if (!options.output.empty()) {
        write_dxil(options.output, dxil);
    }
    return 0;
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        return static_cast<int>(run(parse_args(argc, argv)));
    } catch (const std::invalid_argument& error) {
        std::fprintf(stderr, "%s\nUsage:\n"
            "  wsx_shader_probe --caps [--warp] [--debug]\n"
            "  wsx_shader_probe --dxc-dir PATH [--warp] [--debug] [--dxil OUTPUT]\n"
            "      [--ray-query | --compute INPUT.hlsl --expect UINT32 [--inputs S0 S1]]\n",
            error.what());
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Shader probe failed: %s\n", error.what());
        return 1;
    }
}
