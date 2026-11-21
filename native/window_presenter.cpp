// Copyright (c) 2026 edupode
// SPDX-License-Identifier: CPAL-1.0
//
// The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
// Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
// The Original Code is AnyXboxSeries.
// The Original Developer is the Initial Developer.
// The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
//

#include "window_presenter.h"

#include <cstdio>
#include <cstring>

namespace wsx {
namespace {
constexpr DXGI_FORMAT frame_format = DXGI_FORMAT_R8G8B8A8_UNORM;

HRESULT graphics_error(const char* operation, HRESULT status) noexcept {
    std::fprintf(stderr, "%s returned 0x%08lX\n", operation, static_cast<unsigned long>(status));
    return status;
}

void transition(ID3D12GraphicsCommandList& commands, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) noexcept {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    commands.ResourceBarrier(1, &barrier);
}
}

WindowPresenter::~WindowPresenter() {
    if (fence_event_) {
        CloseHandle(fence_event_);
    }
}

HRESULT WindowPresenter::initialize(ID3D12Device& device, HWND window, UINT width,
                                    UINT height, bool capture_pixels) noexcept {
    width_ = width;
    height_ = height;

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HRESULT status = device.CreateCommandQueue(&queue_desc, IID_PPV_ARGS(queue_.GetAddressOf()));
    if (FAILED(status)) {
        return graphics_error("CreateCommandQueue", status);
    }

    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    status = CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(status)) {
        return graphics_error("CreateDXGIFactory2", status);
    }
    DXGI_SWAP_CHAIN_DESC1 swap_desc{};
    swap_desc.Width = width;
    swap_desc.Height = height;
    swap_desc.Format = frame_format;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.BufferCount = static_cast<UINT>(buffers_.size());
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap;
    status = factory->CreateSwapChainForHwnd(queue_.Get(), window, &swap_desc, nullptr,
                                             nullptr, swap.GetAddressOf());
    if (FAILED(status)) {
        return graphics_error("CreateSwapChainForHwnd", status);
    }
    status = swap.As(&swap_chain_);
    if (FAILED(status)) {
        return graphics_error("Query IDXGISwapChain3", status);
    }
    status = factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(status)) {
        return graphics_error("MakeWindowAssociation", status);
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.NumDescriptors = swap_desc.BufferCount;
    status = device.CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(rtv_heap_.GetAddressOf()));
    if (FAILED(status)) {
        return graphics_error("Create RTV descriptor heap", status);
    }
    auto descriptor = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    const UINT stride = device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (UINT index = 0; index < swap_desc.BufferCount; ++index) {
        status = swap_chain_->GetBuffer(index, IID_PPV_ARGS(buffers_[index].GetAddressOf()));
        if (FAILED(status)) {
            return graphics_error("Get swap chain buffer", status);
        }
        device.CreateRenderTargetView(buffers_[index].Get(), nullptr, descriptor);
        rtv_[index] = descriptor;
        descriptor.ptr += stride;
    }

    status = device.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(allocator_.GetAddressOf()));
    if (FAILED(status)) {
        return graphics_error("CreateCommandAllocator", status);
    }
    status = device.CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(),
                                      nullptr, IID_PPV_ARGS(commands_.GetAddressOf()));
    if (FAILED(status)) {
        return graphics_error("CreateCommandList", status);
    }
    status = commands_->Close();
    if (FAILED(status)) {
        return graphics_error("Close initial command list", status);
    }

    status = device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.GetAddressOf()));
    if (FAILED(status)) {
        return graphics_error("CreateFence", status);
    }
    fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fence_event_) {
        return graphics_error("CreateEventW", HRESULT_FROM_WIN32(GetLastError()));
    }

    if (capture_pixels) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        status = device.CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(readback_.GetAddressOf()));
        if (FAILED(status)) {
            return graphics_error("Create pixel readback buffer", status);
        }
    }
    return S_OK;
}

HRESULT WindowPresenter::draw(const FrameColor& background,
                              std::span<const FrameRectangle> rectangles,
                              PixelReadback* pixel) noexcept {
    if (pixel && (!readback_ || pixel->x >= width_ || pixel->y >= height_)) {
        return E_INVALIDARG;
    }
    HRESULT status = allocator_->Reset();
    if (FAILED(status)) {
        return graphics_error("Reset command allocator", status);
    }
    status = commands_->Reset(allocator_.Get(), nullptr);
    if (FAILED(status)) {
        return graphics_error("Reset command list", status);
    }

    const UINT index = swap_chain_->GetCurrentBackBufferIndex();
    auto* buffer = buffers_[index].Get();
    transition(*commands_.Get(), buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commands_->ClearRenderTargetView(rtv_[index], background.data(), 0, nullptr);
    for (const auto& rectangle : rectangles) {
        commands_->ClearRenderTargetView(rtv_[index], rectangle.color.data(), 1, &rectangle.bounds);
    }

    auto previous_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    if (pixel) {
        transition(*commands_.Get(), buffer, previous_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = buffer;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = readback_.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint.Footprint = {frame_format, 1, 1, 1, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT};
        const D3D12_BOX area{pixel->x, pixel->y, 0, pixel->x + 1, pixel->y + 1, 1};
        commands_->CopyTextureRegion(&destination, 0, 0, 0, &source, &area);
        previous_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    transition(*commands_.Get(), buffer, previous_state, D3D12_RESOURCE_STATE_PRESENT);
    status = commands_->Close();
    if (FAILED(status)) {
        return graphics_error("Close frame command list", status);
    }

    ID3D12CommandList* submitted[]{commands_.Get()};
    queue_->ExecuteCommandLists(1, submitted);
    const HRESULT presented = swap_chain_->Present(pixel ? 0 : 1, 0);
    status = queue_->Signal(fence_.Get(), ++fence_value_);
    if (FAILED(status)) {
        return graphics_error("Signal frame fence", status);
    }
    status = fence_->SetEventOnCompletion(fence_value_, fence_event_);
    if (FAILED(status)) {
        return graphics_error("SetEventOnCompletion", status);
    }
    const DWORD wait = WaitForSingleObject(fence_event_, 10000);
    if (wait != WAIT_OBJECT_0) {
        const DWORD error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        return graphics_error("Wait for completed frame", HRESULT_FROM_WIN32(error));
    }
    if (FAILED(presented)) {
        return graphics_error("Present", presented);
    }

    if (pixel) {
        const D3D12_RANGE read_range{0, pixel->rgba.size()};
        void* mapped = nullptr;
        status = readback_->Map(0, &read_range, &mapped);
        if (FAILED(status)) {
            return graphics_error("Map pixel readback", status);
        }
        std::memcpy(pixel->rgba.data(), mapped, pixel->rgba.size());
        const D3D12_RANGE written_range{0, 0};
        readback_->Unmap(0, &written_range);
    }
    return presented;
}
}
