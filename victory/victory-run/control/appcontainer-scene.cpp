#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <psapi.h>
#include <userenv.h>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <map>
#include <string>
#include <vector>

static_assert(sizeof(void*) == 8, "VIctory memory diagnostics require an x64 controller");

struct Handle {
    HANDLE id = nullptr;
    ~Handle() { if (id && id != INVALID_HANDLE_VALUE) CloseHandle(id); }
};

struct Profile {
    std::wstring name = L"VictoryBootProbe-" + std::to_wstring(GetCurrentProcessId())
        + L"-" + std::to_wstring(GetTickCount64());
    PSID sid = nullptr;
    ~Profile() { if (sid) { DeleteAppContainerProfile(name.c_str()); FreeSid(sid); } }
};

struct AccessGrant {
    std::wstring path;
    PACL original = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_DESCRIPTOR_CONTROL control{};
    bool changed = false;
    DWORD grant(const std::wstring& target, PSID sid, DWORD rights, DWORD inheritance) {
        path = target;
        DWORD error = GetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
            nullptr, nullptr, &original, nullptr, &descriptor);
        if (error != ERROR_SUCCESS) return error;
        DWORD revision;
        if (!GetSecurityDescriptorControl(descriptor, &control, &revision)) return GetLastError();
        if (!original) return ERROR_INVALID_ACL;
        EXPLICIT_ACCESSW entry{};
        entry.grfAccessPermissions = rights;
        entry.grfAccessMode = GRANT_ACCESS;
        entry.grfInheritance = inheritance;
        entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entry.Trustee.ptstrName = static_cast<LPWSTR>(sid);
        PACL updated = nullptr;
        error = SetEntriesInAclW(1, &entry, original, &updated);
        if (error != ERROR_SUCCESS) return error;
        error = SetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
            nullptr, nullptr, updated, nullptr);
        LocalFree(updated);
        changed = error == ERROR_SUCCESS;
        return error;
    }
    ~AccessGrant() {
        if (changed) {
            DWORD flags = DACL_SECURITY_INFORMATION;
            if (control & SE_DACL_PROTECTED) flags |= PROTECTED_DACL_SECURITY_INFORMATION;
            DWORD error = SetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, flags,
                nullptr, nullptr, original, nullptr);
            if (error != ERROR_SUCCESS) std::fwprintf(stderr, L"ACL restore failed %lu for %ls\n", error, path.c_str());
        }
        if (descriptor) LocalFree(descriptor);
    }
};

static std::wstring quote(const std::wstring& value) {
    std::wstring out = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') { out.append(slashes * 2 + 1, L'\\'); out.push_back(c); slashes = 0; continue; }
        out.append(slashes, L'\\'); slashes = 0; out.push_back(c);
    }
    out.append(slashes * 2, L'\\'); out.push_back(L'\"');
    return out;
}

struct Attributes {
    std::vector<unsigned char> buffer;
    PPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
    bool initialize() {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 2, 0, &bytes);
        buffer.resize(bytes);
        auto* storage = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
        if (!InitializeProcThreadAttributeList(storage, 2, 0, &bytes)) return false;
        list = storage;
        return true;
    }
    ~Attributes() { if (list) DeleteProcThreadAttributeList(list); }
};

static int failure(const char* stage, DWORD code = GetLastError()) {
    std::fprintf(stderr, "%s failed (0x%08lX)\n", stage, code);
    return 1;
}

static bool verify_token(HANDLE process, PSID expected) {
    Handle token;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token.id)) return false;
    DWORD size = 0;
    BOOL is_appcontainer = FALSE;
    if (!GetTokenInformation(token.id, TokenIsAppContainer, &is_appcontainer, sizeof(is_appcontainer), &size)
        || !is_appcontainer) return false;
    std::vector<unsigned char> buffer(4096);
    if (!GetTokenInformation(token.id, TokenCapabilities, buffer.data(), static_cast<DWORD>(buffer.size()), &size)
        || reinterpret_cast<TOKEN_GROUPS*>(buffer.data())->GroupCount != 0) return false;
    if (!GetTokenInformation(token.id, TokenAppContainerSid, buffer.data(), static_cast<DWORD>(buffer.size()), &size))
        return false;
    return EqualSid(reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION*>(buffer.data())->TokenAppContainer, expected);
}

static void memory_sample(HANDLE process, HANDLE output, ULONGLONG elapsed_ms) {
    if (!output || output == INVALID_HANDLE_VALUE) return;
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    PROCESS_MEMORY_COUNTERS_EX usage{};
    usage.cb = sizeof(usage);
    PERFORMANCE_INFORMATION system{};
    system.cb = sizeof(system);
    if (!GlobalMemoryStatusEx(&memory)
        || !GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&usage), sizeof(usage))
        || !GetPerformanceInfo(&system, sizeof(system))) {
        return;
    }
    if (usage.PrivateUsage < 64ull * 1024 * 1024) return;
    char line[256]{};
    const int length = std::snprintf(
        line, sizeof(line),
        "t_seconds=%llu private_commit_mib=%llu working_set_mib=%llu available_ram_mib=%llu system_commit_mib=%llu system_commit_limit_mib=%llu\r\n",
        static_cast<unsigned long long>(elapsed_ms / 1000),
        static_cast<unsigned long long>(usage.PrivateUsage / (1024 * 1024)),
        static_cast<unsigned long long>(usage.WorkingSetSize / (1024 * 1024)),
        static_cast<unsigned long long>(memory.ullAvailPhys / (1024 * 1024)),
        static_cast<unsigned long long>(system.CommitTotal) * static_cast<unsigned long long>(system.PageSize) / (1024ull * 1024ull),
        static_cast<unsigned long long>(system.CommitLimit) * static_cast<unsigned long long>(system.PageSize) / (1024ull * 1024ull));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(line)) return;
    DWORD written = 0;
    WriteFile(output, line, static_cast<DWORD>(length), &written, nullptr);
    std::printf("%s", line);
    std::fflush(stdout);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 5) return failure("usage: controller <stage> <read-only package dir> <timeout seconds> <app executable> [args...]", ERROR_BAD_ARGUMENTS);
    const std::wstring stage = argv[1];
    const std::wstring package = argv[2];
    const std::wstring app = stage + L"\\app";
    const std::wstring control = stage + L"\\control";
    const std::wstring state = stage + L"\\state";
    wchar_t* timeout_end = nullptr;
    const auto seconds = std::wcstoul(argv[3], &timeout_end, 10);
    if (timeout_end == argv[3] || *timeout_end || seconds > 3600)
        return failure("timeout argument", ERROR_BAD_ARGUMENTS);
    const std::wstring exe = argv[4];
    std::wstring command = quote(exe);
    for (int i = 5; i < argc; ++i) command += L" " + quote(argv[i]);

    Profile profile;
    const HRESULT hr = CreateAppContainerProfile(profile.name.c_str(), L"VIctory local boot probe",
        L"Temporary offline application isolation", nullptr, 0, &profile.sid);
    if (FAILED(hr)) return failure("CreateAppContainerProfile", static_cast<DWORD>(hr));

    AccessGrant root_access, app_access, state_access, package_access;
    constexpr DWORD inherit = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
    constexpr DWORD read_execute = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
    const DWORD root_rights = FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE;
    DWORD error = root_access.grant(stage, profile.sid, root_rights, NO_INHERITANCE);
    if (error) return failure("stage ACL", error);
    error = app_access.grant(app, profile.sid, read_execute, inherit);
    if (error) return failure("app ACL", error);
    error = state_access.grant(state, profile.sid, FILE_ALL_ACCESS, inherit);
    if (error) return failure("state ACL", error);
    error = package_access.grant(package, profile.sid, read_execute, inherit);
    if (error) return failure("package read-only ACL", error);

    SECURITY_ATTRIBUTES inheritable{sizeof(inheritable), nullptr, TRUE};
    Handle log;
    log.id = CreateFileW((control + L"\\launch.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        &inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log.id == INVALID_HANDLE_VALUE) return failure("create log");
    Handle input;
    input.id = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input.id == INVALID_HANDLE_VALUE) return failure("open null input");

    Attributes attrs;
    if (!attrs.initialize()) return failure("attribute initialization");
    SECURITY_CAPABILITIES caps{};
    caps.AppContainerSid = profile.sid;
    if (!UpdateProcThreadAttribute(attrs.list, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
        &caps, sizeof(caps), nullptr, nullptr)) return failure("AppContainer attribute");
    HANDLE inherited[] = {log.id, input.id};
    if (!UpdateProcThreadAttribute(attrs.list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherited, sizeof(inherited), nullptr, nullptr)) return failure("handle allowlist");

    Handle job;
    job.id = CreateJobObjectW(nullptr, nullptr);
    if (!job.id) return failure("job creation");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.id, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return failure("job lifetime setup");
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
    ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_READCLIPBOARD | JOB_OBJECT_UILIMIT_WRITECLIPBOARD;
    if (!SetInformationJobObject(job.id, JobObjectBasicUIRestrictions, &ui, sizeof(ui)))
        return failure("clipboard restriction");

    wchar_t windows_dir[MAX_PATH]{};
    if (!GetWindowsDirectoryW(windows_dir, MAX_PATH)) return failure("Windows directory");
    const std::wstring system_root(windows_dir);
    std::map<std::wstring, std::wstring> values{
        {L"APPDATA", state}, {L"LOCALAPPDATA", state}, {L"PATH", system_root + L"\\System32"},
        {L"SystemRoot", system_root}, {L"TEMP", state}, {L"TMP", state},
        {L"USERPROFILE", state}, {L"VICTORY_CONFIG", app + L"\\victory.toml"},
        {L"windir", system_root}, {L"VICTORY_D3D12_HIGH_PERFORMANCE", L"1"}, {L"VICTORY_SUBMIT_MAX_RUNS", L"1"}, {L"VICTORY_D3D12_DRED", L"1"}, {L"VICTORY_LOG", L"info"}
    };
    // Only the diagnostic launcher can select the bounded scene comparison.
    wchar_t wait_ms[5]{};
    const DWORD wait_length = GetEnvironmentVariableW(L"VICTORY_PIPELINE_WAIT_MS", wait_ms, 5);
    if (wait_length) {
        if (wait_length >= 5 || (std::wcscmp(wait_ms, L"500") && std::wcscmp(wait_ms, L"2500")))
            return failure("expected a 500 or 2500 ms pipeline budget", ERROR_BAD_ARGUMENTS);
        values.emplace(L"VICTORY_PIPELINE_WAIT_MS", wait_ms);
        wchar_t preview_flag[2]{};
        const bool preview = GetEnvironmentVariableW(L"VICTORY_SCENE_PREVIEW", preview_flag, 2) == 1
            && preview_flag[0] == L'1';
        values.emplace(L"VICTORY_CAPTURE", state + (preview ? L"\\scene-preview" : L"\\scene-probe"));
        if (preview) {
            values.emplace(L"VICTORY_SCENE_PREVIEW", L"1");
            values.emplace(L"VICTORY_WINDOW_TAILS", L"1");
        }
        values.emplace(L"VICTORY_SCENE_PROBE", L"1");
        // Smaller allocation units for views with known byte ranges. Root
        // views without a length retain the runtime's full address range.
        values.emplace(L"VICTORY_WINDOW_KB", L"256");
    }
    constexpr DWORD hash_digits = 16;
    wchar_t skip_hash[hash_digits + 1]{};
    const DWORD skip_len = GetEnvironmentVariableW(
        L"VICTORY_SKIP_PIPELINES", skip_hash, hash_digits + 1);
    if (skip_len != 0) {
        if (skip_len != hash_digits)
            return failure("pipeline diagnostic expects one shader hash", ERROR_BAD_ARGUMENTS);
        for (DWORD i = 0; i < hash_digits; ++i) {
            const wchar_t c = skip_hash[i];
            if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f')
                  || (c >= L'A' && c <= L'F')))
                return failure("pipeline diagnostic expects a hexadecimal hash", ERROR_BAD_ARGUMENTS);
        }
        values.emplace(L"VICTORY_SKIP_PIPELINES", skip_hash);
    }
    Handle memory_log;
    const std::wstring memory_log_path = control + L"\\memory-trend.log";
    memory_log.id = CreateFileW(
        memory_log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (memory_log.id == INVALID_HANDLE_VALUE) {
        memory_log.id = nullptr;
        std::fprintf(stderr, "Could not create memory-trend.log (Win32 %lu).\n", GetLastError());
    }
    std::wstring env;
    for (const auto& [key, value] : values) {
        env += key + L"=" + value;
        env.push_back(L'\0');
    }
    env.push_back(L'\0');

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.id;
    startup.StartupInfo.hStdOutput = log.id;
    startup.StartupInfo.hStdError = log.id;
    startup.lpAttributeList = attrs.list;
    PROCESS_INFORMATION info{};
    const DWORD flags = CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW;
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, flags,
        env.data(), app.c_str(), &startup.StartupInfo, &info)) return failure("isolated process creation");
    Handle process{info.hProcess};
    Handle thread{info.hThread};
    if (!AssignProcessToJobObject(job.id, process.id) || !verify_token(process.id, profile.sid)) {
        DWORD unexpected_error = GetLastError();
        TerminateProcess(process.id, 1);
        return failure("job and token verification", unexpected_error);
    }
    std::printf("pid=%lu appcontainer=true capabilities=0; starting\n", info.dwProcessId);
    std::fflush(stdout);
    if (ResumeThread(thread.id) == MAXDWORD) return failure("isolated process resume");
    const DWORD timeout = seconds == 0 ? INFINITE : static_cast<DWORD>(seconds * 1000);
    DWORD wait;
    bool low_memory_stop = false;
    constexpr ULONGLONG emergency_free_ram = 2048ull * 1024 * 1024;
    const bool timed = timeout != INFINITE;
    const ULONGLONG run_started = GetTickCount64();
    const ULONGLONG deadline = run_started + timeout;
    ULONGLONG next_memory_sample = run_started + 5000;
    unsigned low_memory_samples = 0;
    bool tried_working_set_trim = false;
    for (;;) {
        const ULONGLONG now = GetTickCount64();
        if (timed && now >= deadline) {
            wait = WAIT_TIMEOUT;
            break;
        }
        const DWORD step = timed && deadline - now < 1000
            ? static_cast<DWORD>(deadline - now) : 1000;
        wait = WaitForSingleObject(process.id, step);
        if (wait != WAIT_TIMEOUT) break;
        const ULONGLONG sampled_at = GetTickCount64();
        if (sampled_at >= next_memory_sample) {
            memory_sample(process.id, memory_log.id, sampled_at - run_started);
            next_memory_sample = sampled_at + 5000;
        }

        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        if (!GlobalMemoryStatusEx(&memory)) continue;
        // Drop the title's pageable working set once under pressure. Windows
        // can reclaim cold pages while the process keeps its allocations.
        // Keep checking the emergency floor even if trimming succeeds.
        if (timed && !tried_working_set_trim && memory.ullAvailPhys < 3584ull * 1024 * 1024) {
            tried_working_set_trim = true;
            const BOOL trimmed = EmptyWorkingSet(process.id);
            std::fprintf(stderr, "Title working-set trim %s at %llu MiB available.\n",
                         trimmed ? "requested" : "unavailable",
                         static_cast<unsigned long long>(memory.ullAvailPhys / (1024 * 1024)));
        }
        if (timed && memory.ullAvailPhys < emergency_free_ram) {
            if (++low_memory_samples == 3) {
                std::fprintf(stderr, "Stopped diagnostic after three emergency-memory samples (%llu MiB available).\n",
                             static_cast<unsigned long long>(memory.ullAvailPhys / (1024 * 1024)));
                memory_sample(process.id, memory_log.id, GetTickCount64() - run_started);
                wait = ERROR_NOT_ENOUGH_MEMORY;
                low_memory_stop = true;
                break;
            }
        } else {
            low_memory_samples = 0;
        }
    }
    if (wait != WAIT_OBJECT_0) {
        TerminateJobObject(job.id, 124);
        WaitForSingleObject(process.id, 10000);
        if (low_memory_stop) {
            failure("diagnostic stopped by low-memory guard", ERROR_NOT_ENOUGH_MEMORY);
            return 8;
        }
        if (wait == WAIT_TIMEOUT) {
            failure("diagnostic time limit reached", ERROR_TIMEOUT);
            return 124;
        }
        return failure("diagnostic process wait", wait);
    }
    DWORD code = 1;
    GetExitCodeProcess(process.id, &code);
    TerminateJobObject(job.id, 0);
    std::printf("isolated exit=0x%08lX\n", code);
    return static_cast<int>(code);
}


