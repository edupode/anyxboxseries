#include <Windows.h>
#include <d3d12.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <optional>
#include <type_traits>

#if !defined(_M_X64)
#error The Xbox imports under test require AMD64.
#endif

namespace {
constexpr char deserialize_name[] = "D3D12CreateRootSignatureDeserializer";
constexpr char serialize_name[] = "D3D12SerializeVersionedRootSignature";
// XboxBvhBuilder sets these bits in the root signature flags. The desktop SDK
// does not define them, so their semantics cannot be preserved by forwarding.
constexpr UINT untranslated_xbox_flags = 0x03000000;

struct DesktopExports {
    decltype(&D3D12CreateRootSignatureDeserializer) deserialize = nullptr;
    decltype(&D3D12SerializeVersionedRootSignature) serialize = nullptr;
    HRESULT load_error = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
};

const DesktopExports& desktop_exports() noexcept {
    static const DesktopExports exports = []() noexcept {
        DesktopExports result;
        std::array<wchar_t, 32768> path{};
        constexpr wchar_t library[] = L"\\d3d12.dll";
        const UINT length = GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
        if (length == 0) {
            result.load_error = HRESULT_FROM_WIN32(GetLastError());
            return result;
        }
        if (length > path.size() - std::size(library)) {
            result.load_error = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
            return result;
        }
        std::copy_n(library, std::size(library), path.data() + length);
        // Keep this reference for the lifetime of the process. The returned COM
        // objects may still use the desktop runtime after a call returns.
        const HMODULE module = LoadLibraryExW(path.data(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) {
            result.load_error = HRESULT_FROM_WIN32(GetLastError());
            return result;
        }
        result.deserialize = std::bit_cast<decltype(result.deserialize)>(
            GetProcAddress(module, deserialize_name));
        result.serialize = std::bit_cast<decltype(result.serialize)>(
            GetProcAddress(module, serialize_name));
        return result;
    }();
    return exports;
}

HRESULT write_result(const char* function, HRESULT status,
                     std::optional<UINT> flags = std::nullopt) noexcept {
    std::array<wchar_t, 32768> path{};
    constexpr wchar_t log_name[] = L"shim_log.txt";
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return status;
    }
    wchar_t* filename = std::wcsrchr(path.data(), L'\\');
    if (!filename || filename + 1 > path.data() + path.size() - std::size(log_name)) {
        return status;
    }
    std::copy_n(log_name, std::size(log_name), filename + 1);

    std::array<char, 160> line{};
    const auto code = static_cast<unsigned long>(status);
    const int bytes = flags
        ? std::snprintf(line.data(), line.size(), "%s HRESULT=0x%08lX RootFlags=0x%08X\r\n",
                        function, code, *flags)
        : std::snprintf(line.data(), line.size(), "%s HRESULT=0x%08lX\r\n", function, code);
    if (bytes <= 0 || static_cast<std::size_t>(bytes) >= line.size()) {
        return status;
    }
    const HANDLE file = CreateFileW(path.data(), FILE_APPEND_DATA,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, line.data(), static_cast<DWORD>(bytes), &written, nullptr);
        CloseHandle(file);
    }
    return status;
}

std::optional<UINT> root_flags(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* description) noexcept {
    if (description) {
        switch (description->Version) {
        case D3D_ROOT_SIGNATURE_VERSION_1_0:
            return description->Desc_1_0.Flags;
        case D3D_ROOT_SIGNATURE_VERSION_1_1:
            return description->Desc_1_1.Flags;
        case D3D_ROOT_SIGNATURE_VERSION_1_2:
            return description->Desc_1_2.Flags;
        default:
            break;
        }
    }
    return std::nullopt;
}
}

extern "C" HRESULT WINAPI WsxCreateRootSignatureDeserializer(
    LPCVOID data, SIZE_T size, REFIID iid, void** deserializer) {
    const auto& desktop = desktop_exports();
    if (!desktop.deserialize) {
        if (deserializer) {
            *deserializer = nullptr;
        }
        return write_result(deserialize_name, desktop.load_error);
    }
    return write_result(deserialize_name, desktop.deserialize(data, size, iid, deserializer));
}

extern "C" HRESULT WINAPI WsxSerializeVersionedRootSignature(
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* description, ID3DBlob** blob, ID3DBlob** error_blob) {
    const auto flags = root_flags(description);
    const bool unsupported = flags && (*flags & untranslated_xbox_flags) != 0;
    const auto& desktop = desktop_exports();
    if (unsupported || !desktop.serialize) {
        if (blob) {
            *blob = nullptr;
        }
        if (error_blob) {
            *error_blob = nullptr;
        }
        return write_result(serialize_name, unsupported ? E_NOTIMPL : desktop.load_error, flags);
    }
    return write_result(serialize_name, desktop.serialize(description, blob, error_blob), flags);
}

static_assert(std::is_same_v<decltype(&WsxCreateRootSignatureDeserializer),
                             decltype(&D3D12CreateRootSignatureDeserializer)>);
static_assert(std::is_same_v<decltype(&WsxSerializeVersionedRootSignature),
                             decltype(&D3D12SerializeVersionedRootSignature)>);
