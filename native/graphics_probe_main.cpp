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

#include <wrl/client.h>

#include <cstdio>
#include <string_view>

int wmain(int argc, wchar_t** argv) {
    AdapterMode adapter_mode = AdapterMode::Preferred;
    const wchar_t* frame_path = nullptr;
    bool present = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if (arg == L"--warp") {
            adapter_mode = AdapterMode::Warp;
        } else if (arg == L"--legacy-dxgi") {
            adapter_mode = AdapterMode::Standard;
        } else if (arg == L"--frame" && i + 1 < argc) {
            frame_path = argv[++i];
        } else if (arg == L"--present") {
            present = true;
        } else {
            std::fprintf(stderr, "Usage: wsx_graphics_probe [--warp | --legacy-dxgi] "
                         "[--present] [--frame output.ppm]\n");
            return 2;
        }
    }
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if (FAILED(create_graphics_device(adapter_mode, device.GetAddressOf()))) {
        return 1;
    }
    HWND window = nullptr;
    if (present) {
        window = CreateWindowExW(0, L"STATIC", L"Direct3D 12 Triangle | AnyXboxSeries by edupode",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 336, 280,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) {
            std::fprintf(stderr, "CreateWindowExW returned Win32 error %lu\n", GetLastError());
            return 1;
        }
        ShowWindow(window, SW_SHOWNOACTIVATE);
    }
    const HRESULT status = verify_graphics_frame(device.Get(), frame_path, window);
    if (window) {
        DestroyWindow(window);
    }
    return FAILED(status) ? 1 : 0;
}
