#pragma once

#include <Windows.h>
#include <d3d12.h>

HRESULT create_graphics_device(bool use_warp, ID3D12Device** device) noexcept;
HRESULT verify_graphics_frame(ID3D12Device* device, const wchar_t* frame_path = nullptr) noexcept;
