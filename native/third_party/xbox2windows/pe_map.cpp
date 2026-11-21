// Map an x64 PE image without LoadLibrary and bind its imports through a host resolver.
// Scope: DIR64 relocations, ordinary and delay imports by name or ordinal, section protections,
// .pdata registration, TLS callbacks and the entry point. Delay imports are bound eagerly: the
// image's delay-load helper would call LoadLibrary and bypass the host resolver. TLS data
// templates are rejected. Header fields are checked against the file or SizeOfImage before use.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

// Keep in step with STATUS in tests/cpu/native_map_check.py.
enum MapStatus : std::uint32_t {
    MAP_OK = 0,
    MAP_IO = 1,
    MAP_FORMAT = 2,
    MAP_MEMORY = 3,
    MAP_RELOCATION = 4,
    MAP_UNRESOLVED_IMPORT = 5,
    MAP_UNSUPPORTED = 6,
    MAP_PROTECT = 7,
    MAP_FUNCTION_TABLE = 8,
    MAP_INIT_FAILED = 9,
};

// Returns the address bound to one import, or null to reject it. name is null for an ordinal.
using Resolver = void*(__cdecl*)(const char* dll, const char* name, std::uint32_t ordinal);

namespace {

constexpr std::uint64_t MAX_FILE = 1ull << 30;

struct Failure {
    std::uint32_t* status;
    char* detail;
    std::size_t capacity;

    template <typename... Args>
    void* operator()(MapStatus code, const char* format, Args... args) const {
        if (status) *status = code;
        if (detail && capacity) std::snprintf(detail, capacity, format, args...);
        return nullptr;
    }
};

bool inside(std::uint64_t offset, std::uint64_t size, std::uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}

const IMAGE_DATA_DIRECTORY& directory(const IMAGE_NT_HEADERS64* nt, int index) {
    static const IMAGE_DATA_DIRECTORY empty{};
    return nt->OptionalHeader.NumberOfRvaAndSizes > static_cast<DWORD>(index)
        ? nt->OptionalHeader.DataDirectory[index] : empty;
}

// A view of the mapped image that rejects any range outside SizeOfImage.
struct Image {
    std::uint8_t* base;
    std::uint64_t size;

    template <typename T>
    T* at(std::uint64_t rva, std::uint64_t count = 1) const {
        return inside(rva, sizeof(T) * count, size) ? reinterpret_cast<T*>(base + rva) : nullptr;
    }

    const char* string(std::uint64_t rva) const {
        if (rva >= size) return nullptr;
        const auto* start = base + rva;
        const bool terminated = std::memchr(start, 0, static_cast<std::size_t>(size - rva)) != nullptr;
        return terminated ? reinterpret_cast<const char*>(start) : nullptr;
    }

    const IMAGE_NT_HEADERS64* headers() const {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        return reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    }
};

DWORD protection(DWORD flags) {
    const bool exec = flags & IMAGE_SCN_MEM_EXECUTE, write = flags & IMAGE_SCN_MEM_WRITE;
    const bool read = flags & IMAGE_SCN_MEM_READ;
    if (exec) return write ? PAGE_EXECUTE_READWRITE : read ? PAGE_EXECUTE_READ : PAGE_EXECUTE;
    return write ? PAGE_READWRITE : read ? PAGE_READONLY : PAGE_NOACCESS;
}

// True if rva lies inside a section mapped as executable.
bool executable(const IMAGE_NT_HEADERS64* nt, std::uint64_t rva) {
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& s = sections[i];
        const DWORD span = s.Misc.VirtualSize ? s.Misc.VirtualSize : s.SizeOfRawData;
        if ((s.Characteristics & IMAGE_SCN_MEM_EXECUTE) && rva >= s.VirtualAddress && rva - s.VirtualAddress < span)
            return true;
    }
    return false;
}

// True if every page of [address, address + size) is committed and currently allows reads.
// Image bounds alone do not make bytes readable: section protections apply, and the image may
// change them at run time, so the live protection is queried at each read, including on release.
// The query is not atomic with the read that follows: another thread reprotecting the same pages
// in between is not handled.
bool readable(const void* address, std::size_t size) {
    auto* cursor = static_cast<const std::uint8_t*>(address);
    const auto* end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(cursor, &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
        const DWORD access = info.Protect & 0xff;
        if ((info.Protect & PAGE_GUARD) || access == PAGE_NOACCESS || access == PAGE_EXECUTE) return false;
        cursor = static_cast<const std::uint8_t*>(info.BaseAddress) + info.RegionSize;
    }
    return true;
}

// True if the page at address is committed and neither guarded nor PAGE_NOACCESS. Unlike
// readable(), this accepts execute-only pages, which can hold exported code GetProcAddress returns.
bool accessible(const void* address) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
    const DWORD access = info.Protect & 0xff;
    return !(info.Protect & PAGE_GUARD) && access != PAGE_NOACCESS;
}

// A NUL-terminated string inside the image, scanned only across readable committed pages.
// Returns null if the string would require reading past the image or an unreadable page.
const char* readable_string(const Image& image, std::uint64_t rva) {
    if (rva >= image.size) return nullptr;
    const auto* start = image.base + rva;
    for (std::uint64_t scanned = 0; scanned < image.size - rva;) {
        const auto* cursor = start + scanned;
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(cursor, &info, sizeof(info)) || info.State != MEM_COMMIT) return nullptr;
        const DWORD access = info.Protect & 0xff;
        if ((info.Protect & PAGE_GUARD) || access == PAGE_NOACCESS || access == PAGE_EXECUTE) return nullptr;
        const auto offset = static_cast<std::uint64_t>(cursor - static_cast<const std::uint8_t*>(info.BaseAddress));
        const std::uint64_t run = info.RegionSize - offset;
        const std::uint64_t left = image.size - rva - scanned;
        const std::uint64_t span = run < left ? run : left;
        if (std::memchr(cursor, 0, static_cast<std::size_t>(span))) return reinterpret_cast<const char*>(start);
        scanned += span;
    }
    return nullptr;
}

constexpr std::uint64_t MAX_TLS_CALLBACKS = 256;
constexpr const char* TOO_MANY_TLS_CALLBACKS = "more than 256 TLS callbacks";
// dlattrRva: descriptor fields hold RVAs instead of virtual addresses. Every x64 linker sets it.
constexpr DWORD DELAY_IMPORT_RVA_BASED = 1;

// TLS callbacks of a mapped, relocated image in table order. Returns null or the reason the list is rejected.
const char* tls_callbacks(const Image& image, const IMAGE_NT_HEADERS64* nt, std::vector<PIMAGE_TLS_CALLBACK>& out) {
    const auto& dir = directory(nt, IMAGE_DIRECTORY_ENTRY_TLS);
    if (!dir.VirtualAddress) return nullptr;
    const auto* tls = image.at<IMAGE_TLS_DIRECTORY64>(dir.VirtualAddress);
    if (!tls) return "TLS directory outside image";
    // Sections are already protected here, both after mapping and on release.
    if (!readable(tls, sizeof(IMAGE_TLS_DIRECTORY64))) return "TLS directory not readable";
    // Each value is read once after its check, so the validated value is the one used.
    const std::uint64_t list = tls->AddressOfCallBacks;
    if (!list) return nullptr;
    const auto base = reinterpret_cast<std::uint64_t>(image.base);
    for (std::uint64_t i = 0; i <= MAX_TLS_CALLBACKS; ++i) {
        const std::uint64_t va = list + 8 * i;
        const auto* slot = va >= base ? image.at<std::uint64_t>(va - base) : nullptr;
        if (!slot) return "TLS callback list outside image";
        if (!readable(slot, sizeof(std::uint64_t))) return "TLS callback list not readable";
        const std::uint64_t callback = *slot;
        if (!callback) return nullptr;
        if (i == MAX_TLS_CALLBACKS) return TOO_MANY_TLS_CALLBACKS;
        if (callback < base || callback - base >= image.size || !executable(nt, callback - base))
            return "TLS callback outside executable section";
        out.push_back(reinterpret_cast<PIMAGE_TLS_CALLBACK>(callback));
    }
    return nullptr;
}

// Notify the image as the Windows loader does for LoadLibrary/FreeLibrary: TLS callbacks in table
// order, then the entry point, with a null reserved argument. Returns the entry point's result.
// Measured on Windows 11 build 26300: LoadLibrary sends nothing to a non-DLL image, neither TLS
// callbacks nor the entry point (for an EXE the entry point is the program itself).
BOOL notify(const Image& image, const IMAGE_NT_HEADERS64* nt, const std::vector<PIMAGE_TLS_CALLBACK>& callbacks,
            DWORD reason) {
    if (!(nt->FileHeader.Characteristics & IMAGE_FILE_DLL)) return TRUE;
    for (auto callback : callbacks) callback(image.base, reason, nullptr);
    if (!nt->OptionalHeader.AddressOfEntryPoint) return TRUE;
    using Entry = BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID);
    const auto entry = reinterpret_cast<Entry>(image.base + nt->OptionalHeader.AddressOfEntryPoint);
    return entry(reinterpret_cast<HINSTANCE>(image.base), reason, nullptr);
}

bool read_file(const wchar_t* path, std::vector<std::uint8_t>& raw) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length{};
    DWORD read = 0;
    bool ok = GetFileSizeEx(file, &length) && length.QuadPart > 0
        && static_cast<std::uint64_t>(length.QuadPart) <= MAX_FILE;
    if (ok) {
        try {
            raw.resize(static_cast<std::size_t>(length.QuadPart));
        } catch (const std::bad_alloc&) {
            ok = false;
        }
    }
    ok = ok && ReadFile(file, raw.data(), static_cast<DWORD>(raw.size()), &read, nullptr) && read == raw.size();
    CloseHandle(file);
    return ok;
}

// Images attached by pe_map_image. pe_map.dll is itself loaded by Windows, so its DllMain receives
// DLL_THREAD_ATTACH/DETACH and forwards them to these images: attach in mapping order, detach in
// reverse mapping order, as measured with two LoadLibraryW copies on Windows 11 build 26300.
//
// Lifetime protocol: each image holds one reference for its registration and one per thread
// notification in flight. DllMain takes those references under mapped_lock and calls the guest
// callbacks after releasing it, so a callback may map or release any image, its own included.
// pe_map_release unregisters the image and drops the registration reference without waiting. The
// image's process detach and unmapping run when the last reference goes, either inside
// pe_map_release or on the thread that finishes the last notification. A notification is admitted
// when its thread takes the reference under the registry lock; none is admitted after the release
// unregisters the image. An admitted notification skips an image already marked released, but one
// whose callback has started finishes: when a TLS callback releases its own image, that image's
// entry point still receives the same thread attach. Addresses are the only identity, so once a
// released address is reused by a new mapping, a stale pointer releases that new mapping.
SRWLOCK mapped_lock = SRWLOCK_INIT;

struct Mapped {
    std::uint8_t* base;
    volatile LONG references;
    volatile LONG released;  // set by pe_map_release; notifiers skip the image from then on
};

struct SharedLock {
    SharedLock() { AcquireSRWLockShared(&mapped_lock); }
    ~SharedLock() { ReleaseSRWLockShared(&mapped_lock); }
    SharedLock(const SharedLock&) = delete;
    SharedLock& operator=(const SharedLock&) = delete;
};

struct ExclusiveLock {
    ExclusiveLock() { AcquireSRWLockExclusive(&mapped_lock); }
    ~ExclusiveLock() { ReleaseSRWLockExclusive(&mapped_lock); }
    ExclusiveLock(const ExclusiveLock&) = delete;
    ExclusiveLock& operator=(const ExclusiveLock&) = delete;
};

std::vector<Mapped*>& mapped_images() {
    static std::vector<Mapped*> images;
    return images;
}

// Notify a mapped image, re-reading its callback list as Windows does; if the list was
// overwritten or made unreadable, the entry point is still notified.
void notify_mapped(std::uint8_t* base, DWORD reason) {
    const auto* nt = Image{base, 0}.headers();
    const Image image{base, nt->OptionalHeader.SizeOfImage};
    std::vector<PIMAGE_TLS_CALLBACK> callbacks;
    try {
        if (tls_callbacks(image, nt, callbacks)) callbacks.clear();
    } catch (const std::bad_alloc&) {
        // Never let an allocation failure escape DllMain or pe_map_release; the entry point is
        // still notified.
        callbacks.clear();
    }
    notify(image, nt, callbacks, reason);
}

// Process detach, function-table removal and unmapping, once no reference remains.
void finalize(std::uint8_t* base) {
    const auto* nt = Image{base, 0}.headers();
    notify_mapped(base, DLL_PROCESS_DETACH);
    const auto& pdata = directory(nt, IMAGE_DIRECTORY_ENTRY_EXCEPTION);
    if (pdata.Size) RtlDeleteFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(base + pdata.VirtualAddress));
    VirtualFree(base, 0, MEM_RELEASE);
}

void drop_reference(Mapped* record) {
    if (InterlockedDecrement(&record->references) != 0) return;
    finalize(record->base);
    delete record;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_THREAD_ATTACH || reason == DLL_THREAD_DETACH) {
        std::vector<Mapped*> snapshot;
        {
            const SharedLock hold;
            try {
                snapshot.reserve(mapped_images().size());
            } catch (const std::bad_alloc&) {
                return TRUE;  // no references were taken, so nothing is left pending
            }
            for (auto* record : mapped_images()) {
                InterlockedIncrement(&record->references);
                snapshot.push_back(record);
            }
        }
        // Guest callbacks run without mapped_lock. An image released meanwhile is still mapped
        // until its last reference is dropped here.
        auto notify_one = [reason](Mapped* record) {
            if (!InterlockedCompareExchange(&record->released, 0, 0)) notify_mapped(record->base, reason);
            drop_reference(record);
        };
        if (reason == DLL_THREAD_ATTACH) {
            for (auto it = snapshot.begin(); it != snapshot.end(); ++it) notify_one(*it);
        } else {
            for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it) notify_one(*it);
        }
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void* pe_map_image(const wchar_t* path, Resolver resolve,
                                                    std::uint32_t* status, char* detail,
                                                    std::size_t capacity) {
    const Failure fail{status, detail, capacity};
    if (status) *status = MAP_OK;
    if (detail && capacity) detail[0] = '\0';

    std::vector<std::uint8_t> raw;
    if (!read_file(path, raw)) return fail(MAP_IO, "cannot read image");
    if (raw.size() < sizeof(IMAGE_DOS_HEADER)) return fail(MAP_FORMAT, "truncated DOS header");
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0
        || !inside(dos->e_lfanew, sizeof(IMAGE_NT_HEADERS64), raw.size()))
        return fail(MAP_FORMAT, "bad DOS header");
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(raw.data() + dos->e_lfanew);
    const auto& opt = nt->OptionalHeader;
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
        || opt.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return fail(MAP_FORMAT, "not an x64 PE32+ image");
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    const std::uint64_t table_end =
        reinterpret_cast<const std::uint8_t*>(sections + nt->FileHeader.NumberOfSections) - raw.data();
    if (nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64)
        || dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > opt.SizeOfHeaders || table_end > opt.SizeOfHeaders
        || opt.SizeOfHeaders > opt.SizeOfImage || opt.SizeOfHeaders > raw.size())
        return fail(MAP_FORMAT, "section table outside headers");

    if (opt.AddressOfEntryPoint && !executable(nt, opt.AddressOfEntryPoint))
        return fail(MAP_FORMAT, "entry point outside executable section");

    // Map away from the preferred base so the relocation path always runs.
    auto allocate = [&] {
        return static_cast<std::uint8_t*>(
            VirtualAlloc(nullptr, opt.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    };
    auto* base = allocate();
    if (base == reinterpret_cast<std::uint8_t*>(opt.ImageBase)) {
        auto* other = allocate();
        VirtualFree(base, 0, MEM_RELEASE);
        base = other;
    }
    if (!base) return fail(MAP_MEMORY, "cannot reserve image");
    const Image image{base, opt.SizeOfImage};
    auto release = [&](void* result) {
        VirtualFree(base, 0, MEM_RELEASE);
        return result;
    };

    std::memcpy(base, raw.data(), opt.SizeOfHeaders);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& s = sections[i];
        const DWORD span = s.Misc.VirtualSize ? s.Misc.VirtualSize : s.SizeOfRawData;
        // Like the Windows loader, a section without a file offset stays zero-filled even when
        // SizeOfRawData is set; it does not take the headers at file offset 0.
        const DWORD copy = !s.PointerToRawData ? 0 : s.SizeOfRawData < span ? s.SizeOfRawData : span;
        if (s.VirtualAddress < opt.SizeOfHeaders || !inside(s.VirtualAddress, span, opt.SizeOfImage)
            || !inside(s.PointerToRawData, copy, raw.size()))
            return release(fail(MAP_FORMAT, "section %.8s outside image", reinterpret_cast<const char*>(s.Name)));
        if (copy) {
            // Like the Windows loader, read whole 0x200-byte file sectors: round the start of the raw
            // data down and its end up. Sector bytes past the end of the file stay zero.
            const std::uint64_t first = s.PointerToRawData & ~std::uint64_t{0x1ff};
            std::uint64_t end = (std::uint64_t{s.PointerToRawData} + s.SizeOfRawData + 0x1ff) & ~std::uint64_t{0x1ff};
            if (end > raw.size()) end = raw.size();
            const std::uint64_t length = end - first < span ? end - first : span;
            std::memcpy(base + s.VirtualAddress, raw.data() + first, static_cast<std::size_t>(length));
        }
    }

    // TLS data needs a slot in each thread's ThreadLocalStoragePointer array, which ntdll owns.
    // Only an empty template is accepted. Unlike Windows, which assigns an index even for an
    // empty template, pe_map leaves AddressOfIndex as linked.
    // Like the Windows loader, the TLS directory is found by its address alone; Size is not used.
    // Measured on Windows 11 build 26200: a TLS directory with Size 0, 1 or 39 still runs its
    // callback, one with address 0 is ignored whatever its Size, and one that does not fit in the
    // image fails the load.
    const auto& tls = directory(nt, IMAGE_DIRECTORY_ENTRY_TLS);
    if (tls.VirtualAddress) {
        const auto* t = image.at<IMAGE_TLS_DIRECTORY64>(tls.VirtualAddress);
        if (!t) return release(fail(MAP_FORMAT, "TLS directory outside image"));
        if (t->EndAddressOfRawData != t->StartAddressOfRawData || t->SizeOfZeroFill)
            return release(fail(MAP_UNSUPPORTED, "TLS data"));
    }

    const auto& imports = directory(nt, IMAGE_DIRECTORY_ENTRY_IMPORT);
    const auto& delay = directory(nt, IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT);

    // Writes below (relocations, IAT) must land in sections, never in the headers or in the
    // directory that drives them, so validated structures cannot change while they are walked.
    auto overlaps = [&](std::uint64_t rva, const IMAGE_DATA_DIRECTORY& driver) {
        const std::uint64_t start = driver.VirtualAddress, end = start + driver.Size;
        return rva + 8 > start && rva < end;
    };
    auto writable = [&](std::uint64_t rva, const IMAGE_DATA_DIRECTORY& driver) {
        return rva >= opt.SizeOfHeaders && image.at<std::uint64_t>(rva) && !overlaps(rva, driver);
    };

    const auto delta = reinterpret_cast<std::uint64_t>(base) - opt.ImageBase;
    const auto& relocs = directory(nt, IMAGE_DIRECTORY_ENTRY_BASERELOC);
    if (nt->FileHeader.Characteristics & IMAGE_FILE_RELOCS_STRIPPED)
        return release(fail(MAP_RELOCATION, "relocations stripped"));
    if (!inside(relocs.VirtualAddress, relocs.Size, opt.SizeOfImage))
        return release(fail(MAP_RELOCATION, "relocation directory outside image"));
    for (std::uint64_t offset = 0; offset + sizeof(IMAGE_BASE_RELOCATION) <= relocs.Size;) {
        const auto* block = image.at<IMAGE_BASE_RELOCATION>(relocs.VirtualAddress + offset);
        const DWORD page = block->VirtualAddress, length = block->SizeOfBlock;
        if (length < sizeof(IMAGE_BASE_RELOCATION) || length > relocs.Size - offset)
            return release(fail(MAP_RELOCATION, "bad relocation block"));
        const auto* entry = reinterpret_cast<const WORD*>(block + 1);
        const DWORD count = (length - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
        for (DWORD k = 0; k < count; ++k) {
            const WORD type = entry[k] >> 12;
            const std::uint64_t target = page + std::uint64_t{entry[k] & 0xfffu};
            if (type == IMAGE_REL_BASED_ABSOLUTE) continue;
            if (type != IMAGE_REL_BASED_DIR64 || !writable(target, relocs))
                return release(fail(MAP_RELOCATION, "unsupported relocation at RVA %#llx",
                                    static_cast<unsigned long long>(target)));
            *image.at<std::uint64_t>(target) += delta;
        }
        offset += length;
    }

    // The delay-import directory is found by its address, like Windows; Size is not used. Walk both
    // tables to their null descriptors before either is bound, so an IAT write cannot change the
    // table the other walk reads.
    std::uint64_t delay_descriptors = 0;
    IMAGE_DATA_DIRECTORY delay_walked{};
    if (delay.VirtualAddress) {
        for (;; ++delay_descriptors) {
            const auto* d = image.at<IMAGE_DELAYLOAD_DESCRIPTOR>(
                delay.VirtualAddress + delay_descriptors * sizeof(IMAGE_DELAYLOAD_DESCRIPTOR));
            if (!d) return release(fail(MAP_FORMAT, "delay import directory outside image"));
            if (!d->DllNameRVA) break;
        }
        // The walked table, terminator included, is what IAT writes must not overlap.
        delay_walked = {delay.VirtualAddress,
                        static_cast<DWORD>((delay_descriptors + 1) * sizeof(IMAGE_DELAYLOAD_DESCRIPTOR))};
    }

    // Like the Windows loader, find the import table by its address and walk it to the null
    // descriptor; the directory Size is not used. Measured on Windows 11 builds 26200 and 26300:
    // copies with Size 0, 19 or 20 still load and bind every slot. Each descriptor is
    // bounds-checked on its own.
    std::uint64_t import_descriptors = 0;
    IMAGE_DATA_DIRECTORY imports_walked{};
    if (imports.VirtualAddress) {
        for (;; ++import_descriptors) {
            const auto* d = image.at<IMAGE_IMPORT_DESCRIPTOR>(
                imports.VirtualAddress + import_descriptors * sizeof(IMAGE_IMPORT_DESCRIPTOR));
            if (!d) return release(fail(MAP_FORMAT, "import directory outside image"));
            if (!d->Name) break;
        }
        // The walked table, terminator included, is what IAT writes must not overlap.
        imports_walked = {imports.VirtualAddress,
                          static_cast<DWORD>((import_descriptors + 1) * sizeof(IMAGE_IMPORT_DESCRIPTOR))};
    }

    // The PE loader leaves delay imports to the image's helper, which calls LoadLibrary and would
    // bypass the host resolver. pe_map has no LoadLibrary, so it resolves each delay descriptor
    // through the same resolver at map time and writes the bound address into its IAT. A descriptor
    // without dlattrRva stores virtual addresses rather than RVAs and is rejected. The directory is
    // found by its address, like Windows; Size is not used, and the table is walked above.
    if (delay_descriptors) {
        if (!resolve) return release(fail(MAP_UNRESOLVED_IMPORT, "delay imports present without a resolver"));
        const auto* desc = image.at<IMAGE_DELAYLOAD_DESCRIPTOR>(delay.VirtualAddress, delay_descriptors + 1);
        if (!desc) return release(fail(MAP_FORMAT, "delay import directory outside image"));
        for (std::uint64_t d = 0; d < delay_descriptors; ++d) {
            if (!(desc[d].Attributes.AllAttributes & DELAY_IMPORT_RVA_BASED))
                return release(fail(MAP_UNSUPPORTED, "delay imports without RVA attributes"));
            const char* name_in_image = image.string(desc[d].DllNameRVA);
            if (!name_in_image) return release(fail(MAP_FORMAT, "delay import DLL name outside image"));
            // Keep a copy: an IAT write below may legally overlap the string in the image.
            const std::string dll_name(name_in_image);
            const char* dll = dll_name.c_str();
            const DWORD lookup = desc[d].ImportNameTableRVA, iat = desc[d].ImportAddressTableRVA;
            if (!lookup || !iat)
                return release(fail(MAP_FORMAT, "delay import tables for %s outside image", dll));
            for (std::uint64_t i = 0;; ++i) {
                const auto* word = image.at<std::uint64_t>(lookup + 8 * i);
                if (!word) return release(fail(MAP_FORMAT, "delay import thunk for %s outside image", dll));
                const std::uint64_t slot = iat + 8 * i;
                if (!writable(slot, delay_walked) || overlaps(slot, imports_walked))
                    return release(fail(MAP_FORMAT, "delay import IAT for %s outside image", dll));
                if (!*word) break;
                const bool by_ordinal = IMAGE_SNAP_BY_ORDINAL64(*word);
                const auto ordinal = by_ordinal ? static_cast<std::uint32_t>(IMAGE_ORDINAL64(*word)) : 0u;
                // Bits 31..62 of a named thunk must be zero: masking them could alias a valid low RVA.
                const std::uint64_t name_rva = by_ordinal ? 0 : *word;
                const auto* hint = by_ordinal || name_rva >> 31 ? nullptr : image.at<WORD>(name_rva);
                const char* name = hint ? image.string(name_rva + sizeof(WORD)) : nullptr;
                if (!by_ordinal && !name)
                    return release(fail(MAP_FORMAT, "delay import name for %s outside image", dll));
                void* bound = resolve(dll, name, ordinal);
                if (!bound && name)
                    return release(fail(MAP_UNRESOLVED_IMPORT, "unresolved delay import %s!%s", dll, name));
                if (!bound)
                    return release(fail(MAP_UNRESOLVED_IMPORT, "unresolved delay import %s!#%u", dll,
                                        static_cast<unsigned>(ordinal)));
                *image.at<std::uint64_t>(slot) = reinterpret_cast<std::uint64_t>(bound);
            }
        }
    }

    if (import_descriptors) {
        if (!resolve) return release(fail(MAP_UNRESOLVED_IMPORT, "imports present without a resolver"));
        const auto* desc = image.at<IMAGE_IMPORT_DESCRIPTOR>(imports.VirtualAddress, import_descriptors + 1);
        for (std::uint64_t d = 0; d < import_descriptors; ++d) {
            const char* name_in_image = image.string(desc[d].Name);
            if (!name_in_image) return release(fail(MAP_FORMAT, "import DLL name outside image"));
            // Keep a copy: an IAT write below may legally overlap the string in the image.
            const std::string dll_name(name_in_image);
            const char* dll = dll_name.c_str();
            const DWORD lookup = desc[d].OriginalFirstThunk ? desc[d].OriginalFirstThunk : desc[d].FirstThunk;
            for (std::uint64_t i = 0;; ++i) {
                const auto* word = image.at<std::uint64_t>(lookup + 8 * i);
                const std::uint64_t target = desc[d].FirstThunk + 8 * i;
                // The delay directory was read above, but a normal IAT write must still not reach it.
                if (!word || !writable(target, imports_walked) || overlaps(target, delay_walked))
                    return release(fail(MAP_FORMAT, "import thunk for %s outside image", dll));
                if (!*word) break;
                const bool by_ordinal = IMAGE_SNAP_BY_ORDINAL64(*word);
                const auto ordinal = by_ordinal ? static_cast<std::uint32_t>(IMAGE_ORDINAL64(*word)) : 0u;
                const auto* hint = by_ordinal ? nullptr : image.at<WORD>(*word & 0x7fffffff);
                const char* name = hint ? image.string((*word & 0x7fffffff) + sizeof(WORD)) : nullptr;
                if (!by_ordinal && !name) return release(fail(MAP_FORMAT, "import name for %s outside image", dll));
                void* bound = resolve(dll, name, ordinal);
                if (!bound && name) return release(fail(MAP_UNRESOLVED_IMPORT, "unresolved import %s!%s", dll, name));
                if (!bound)
                    return release(fail(MAP_UNRESOLVED_IMPORT, "unresolved import %s!#%u", dll,
                                        static_cast<unsigned>(ordinal)));
                *image.at<std::uint64_t>(target) = reinterpret_cast<std::uint64_t>(bound);
            }
        }
    }

    // Record the actual base in the mapped headers, as the Windows loader does.
    reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew)->OptionalHeader.ImageBase =
        reinterpret_cast<std::uint64_t>(base);
    DWORD old = 0;
    if (!VirtualProtect(base, opt.SizeOfHeaders, PAGE_READONLY, &old))
        return release(fail(MAP_PROTECT, "cannot protect headers"));
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& s = sections[i];
        const DWORD span = s.Misc.VirtualSize ? s.Misc.VirtualSize : s.SizeOfRawData;
        if (span && !VirtualProtect(base + s.VirtualAddress, span, protection(s.Characteristics), &old))
            return release(fail(MAP_PROTECT, "cannot protect section %.8s", reinterpret_cast<const char*>(s.Name)));
    }
    FlushInstructionCache(GetCurrentProcess(), base, opt.SizeOfImage);

    const auto& pdata = directory(nt, IMAGE_DIRECTORY_ENTRY_EXCEPTION);
    if (pdata.Size) {
        auto* table = image.at<RUNTIME_FUNCTION>(pdata.VirtualAddress, pdata.Size / sizeof(RUNTIME_FUNCTION));
        if (!table || !RtlAddFunctionTable(table, pdata.Size / sizeof(RUNTIME_FUNCTION),
                                           reinterpret_cast<DWORD64>(base)))
            return release(fail(MAP_FUNCTION_TABLE, "cannot register .pdata"));
    }

    std::vector<PIMAGE_TLS_CALLBACK> callbacks;
    const auto* mapped_nt = image.headers();
    if (const char* reason = tls_callbacks(image, mapped_nt, callbacks)) {
        if (pdata.Size) RtlDeleteFunctionTable(image.at<RUNTIME_FUNCTION>(pdata.VirtualAddress));
        const bool limit = reason == TOO_MANY_TLS_CALLBACKS;
        return release(fail(limit ? MAP_UNSUPPORTED : MAP_FORMAT, "%s", reason));
    }
    if (!notify(image, mapped_nt, callbacks, DLL_PROCESS_ATTACH)) {
        // Measured on Windows: a failed attach is followed by a full detach before the image is freed.
        // The callback list collected before attach is reused here; only pe_map_release re-reads it.
        notify(image, mapped_nt, callbacks, DLL_PROCESS_DETACH);
        if (pdata.Size) RtlDeleteFunctionTable(image.at<RUNTIME_FUNCTION>(pdata.VirtualAddress));
        return release(fail(MAP_INIT_FAILED, "entry point returned FALSE"));
    }
    Mapped* record = new (std::nothrow) Mapped{base, 1, 0};
    bool registered = record != nullptr;
    if (record) {
        const ExclusiveLock hold;
        try {
            mapped_images().push_back(record);
        } catch (const std::bad_alloc&) {
            registered = false;
        }
    }
    if (!registered) {
        delete record;
        notify(image, mapped_nt, callbacks, DLL_PROCESS_DETACH);
        if (pdata.Size) RtlDeleteFunctionTable(image.at<RUNTIME_FUNCTION>(pdata.VirtualAddress));
        return release(fail(MAP_MEMORY, "cannot register image for thread notifications"));
    }
    return base;
}

// Only valid for a base returned by pe_map_image, whose headers were validated and are read-only.
extern "C" __declspec(dllexport) void* pe_map_export(void* mapped, const char* name) {
    const auto* nt = Image{static_cast<std::uint8_t*>(mapped), 0}.headers();
    const Image image{static_cast<std::uint8_t*>(mapped), nt->OptionalHeader.SizeOfImage};
    const auto& exports = directory(nt, IMAGE_DIRECTORY_ENTRY_EXPORT);
    const auto* dir = exports.Size ? image.at<IMAGE_EXPORT_DIRECTORY>(exports.VirtualAddress) : nullptr;
    // A mapped section can lack read access, by its characteristics or after a run-time reprotect.
    // Image bounds alone do not make the export tables readable, so every read below is checked
    // against the live protection first, as the TLS path already does.
    if (!dir || !readable(dir, sizeof(IMAGE_EXPORT_DIRECTORY))) return nullptr;
    const auto* names = image.at<DWORD>(dir->AddressOfNames, dir->NumberOfNames);
    const auto* ordinals = image.at<WORD>(dir->AddressOfNameOrdinals, dir->NumberOfNames);
    const auto* functions = image.at<DWORD>(dir->AddressOfFunctions, dir->NumberOfFunctions);
    if (!names || !ordinals || !functions) return nullptr;
    if (!readable(names, sizeof(DWORD) * dir->NumberOfNames) ||
        !readable(ordinals, sizeof(WORD) * dir->NumberOfNames) ||
        !readable(functions, sizeof(DWORD) * dir->NumberOfFunctions)) return nullptr;
    for (DWORD i = 0; i < dir->NumberOfNames; ++i) {
        const char* exported = readable_string(image, names[i]);
        if (!exported || std::strcmp(exported, name) != 0) continue;
        if (ordinals[i] >= dir->NumberOfFunctions) return nullptr;
        const DWORD rva = functions[ordinals[i]];
        // Forwarded exports point back into the export directory; they are not code here.
        if (rva >= exports.VirtualAddress && rva - exports.VirtualAddress < exports.Size) return nullptr;
        // An empty slot (RVA 0) names no code; returning the image base would be wrong. An exported
        // address must be accessible, but it may be execute-only, so readable() is not used here.
        return rva && rva < image.size && accessible(image.base + rva) ? image.base + rva : nullptr;
    }
    return nullptr;
}

// Returns 1 after unregistering an image that pe_map_image returned, and 0 for null, for any
// other pointer and for a second release. Process detach and unmapping follow the protocol above:
// at once if no thread notification for the image is in flight, otherwise when the last one ends.
extern "C" __declspec(dllexport) int pe_map_release(void* mapped) {
    Mapped* record = nullptr;
    {
        const ExclusiveLock hold;
        auto& images = mapped_images();
        for (auto it = images.begin(); it != images.end(); ++it) {
            if ((*it)->base == mapped) {
                record = *it;
                images.erase(it);
                break;
            }
        }
    }
    if (!record) return 0;
    InterlockedExchange(&record->released, 1);
    drop_reference(record);
    return 1;
}

// Start a mapped EXE on the calling thread the way the loader starts a process: TLS callbacks in table
// order with DLL_PROCESS_ATTACH and a null reserved argument, then the entry point, whose return value is
// the exit code Windows would use. Measured on Windows 11 build 26300 with tests/cpu/exe_start.asm.
//
// Preconditions, none of which is checked:
// - `mapped` is a base returned by pe_map_image, still live and not yet released. Like pe_map_export,
//   this reads the headers of the argument directly, so any other pointer is undefined behavior.
// - The caller trusts the image. The entry point and the callbacks are arbitrary machine code that runs
//   in the host process with the host's privileges. They can crash, hang or terminate it (an entry point
//   that calls ExitProcess ends the host), and they run on the calling thread, so their stack, TLS and
//   loader-lock state are the caller's. Run untrusted or unknown images in a child process.
// pe_map_image maps an EXE without notifying it, like LoadLibrary, so nothing has run before this call.
// Start once per mapping. The caller keeps it live throughout the call and prevents any thread,
// TLS callback or entry point from re-entering run or releasing this mapping during startup.
//
// Returns 0 without running anything for a DLL, which pe_map_image already notified, for an image without
// an entry point, for a null exit_code and for an unreadable TLS callback list. The PEB argument that
// Windows passes to the entry point is not forwarded, and no process-detach notification follows because
// the process goes on.
extern "C" __declspec(dllexport) int pe_map_run(void* mapped, std::uint32_t* exit_code) {
    auto* base = static_cast<std::uint8_t*>(mapped);
    const auto* nt = Image{base, 0}.headers();
    if (!exit_code || (nt->FileHeader.Characteristics & IMAGE_FILE_DLL) || !nt->OptionalHeader.AddressOfEntryPoint)
        return 0;
    const Image image{base, nt->OptionalHeader.SizeOfImage};
    std::vector<PIMAGE_TLS_CALLBACK> callbacks;
    if (tls_callbacks(image, nt, callbacks)) return 0;
    for (auto callback : callbacks) callback(base, DLL_PROCESS_ATTACH, nullptr);
    using Entry = DWORD(WINAPI*)(void*);
    *exit_code = reinterpret_cast<Entry>(base + nt->OptionalHeader.AddressOfEntryPoint)(nullptr);
    return 1;
}
