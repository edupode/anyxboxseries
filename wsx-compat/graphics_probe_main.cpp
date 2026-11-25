#include "graphics_runtime.h"

#include <wrl/client.h>

#include <cstdio>
#include <string_view>

int wmain(int argc, wchar_t** argv) {
    bool use_warp = false;
    const wchar_t* frame_path = nullptr;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if (arg == L"--warp") {
            use_warp = true;
        } else if (arg == L"--frame" && i + 1 < argc) {
            frame_path = argv[++i];
        } else {
            std::fprintf(stderr, "Usage: wsx_graphics_probe [--warp] [--frame output.ppm]\n");
            return 2;
        }
    }
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if (FAILED(create_graphics_device(use_warp, device.GetAddressOf()))) {
        return 1;
    }
    return FAILED(verify_graphics_frame(device.Get(), frame_path)) ? 1 : 0;
}
