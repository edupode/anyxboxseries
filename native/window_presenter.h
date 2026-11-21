// Copyright (c) 2026 edupode
// SPDX-License-Identifier: CPAL-1.0
//
// The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
// Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
// The Original Code is AnyXboxSeries.
// The Original Developer is the Initial Developer.
// The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
//

#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <span>

namespace wsx {
using FrameColor = std::array<float, 4>;

struct FrameRectangle {
    D3D12_RECT bounds;
    FrameColor color;
};

struct PixelReadback {
    UINT x;
    UINT y;
    std::array<std::uint8_t, 4> rgba{};
};

class WindowPresenter {
public:
    WindowPresenter() = default;
    ~WindowPresenter();
    WindowPresenter(const WindowPresenter&) = delete;
    WindowPresenter& operator=(const WindowPresenter&) = delete;

    HRESULT initialize(ID3D12Device& device, HWND window, UINT width,
                       UINT height, bool capture_pixels) noexcept;
    HRESULT draw(const FrameColor& background, std::span<const FrameRectangle> rectangles,
                 PixelReadback* pixel = nullptr) noexcept;

private:
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> buffers_;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2> rtv_{};
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    HANDLE fence_event_ = nullptr;
    UINT64 fence_value_ = 0;
    UINT width_ = 0;
    UINT height_ = 0;
};
}
