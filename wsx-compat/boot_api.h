#pragma once

#include <Windows.h>
#include <d3d12.h>

// Both arguments are borrowed for the duration of the call. The package path
// is absolute when supplied. The module must finish its work before returning.
using WsxStartV1Proc = HRESULT (WINAPI*)(ID3D12Device* device, LPCWSTR package_directory) noexcept;
inline constexpr char wsx_start_symbol[] = "WsxStartV1";
