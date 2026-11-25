#include "boot_api.h"
#include "graphics_runtime.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <type_traits>

extern "C" __declspec(dllexport) HRESULT WINAPI WsxStartV1(
    ID3D12Device* device, LPCWSTR package_directory) noexcept {
    if (!device) {
        return E_POINTER;
    }
    if (package_directory) {
        try {
            const auto path = std::filesystem::path(package_directory) / L"MicrosoftGame.Config";
            std::ifstream manifest(path, std::ios::binary);
            if (!manifest || manifest.peek() == std::ifstream::traits_type::eof()) {
                std::fprintf(stderr, "Could not read %ls\n", path.c_str());
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            std::puts("Module opened the package manifest for reading.");
        } catch (const std::exception& error) {
            std::fprintf(stderr, "%s\n", error.what());
            return E_FAIL;
        }
    }
    const HRESULT status = verify_graphics_frame(device);
    std::fflush(stdout);
    return status;
}

static_assert(std::is_same_v<decltype(&WsxStartV1), WsxStartV1Proc>);
