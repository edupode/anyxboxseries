// Copyright (c) 2026 edupode
// SPDX-License-Identifier: CPAL-1.0
//
// The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
// Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
// The Original Code is AnyXboxSeries.
// The Original Developer is the Initial Developer.
// The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
//

#include <Windows.h>

#include <fcntl.h>
#include <io.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using WinHandle = std::unique_ptr<void, decltype(&CloseHandle)>;

struct DebugSession {
    DWORD pid;
    ~DebugSession() {
        if (pid) {
            DebugActiveProcessStop(pid);
        }
    }
};

DWORD observation_time(std::wstring_view text) {
    DWORD value = 0;
    for (wchar_t character : text) {
        if (character < L'0' || character > L'9') {
            throw std::invalid_argument("--observe-ms expects digits.");
        }
        value = value * 10 + static_cast<DWORD>(character - L'0');
        if (value > 300000) {
            break;
        }
    }
    if (value == 0 || value > 300000) {
        throw std::invalid_argument("--observe-ms must be between 1 and 300000.");
    }
    return value;
}

void append_argument(std::wstring& command, std::wstring_view argument) {
    if (!command.empty()) {
        command += L' ';
    }
    command += L'"';
    std::size_t slashes = 0;
    for (wchar_t character : argument) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        command.append(character == L'"' ? 2 * slashes + 1 : slashes, L'\\');
        command += character;
        slashes = 0;
    }
    command.append(2 * slashes, L'\\');
    command += L'"';
}

const wchar_t* status_name(DWORD status) {
    switch (status) {
    case 0xC000007B: return L"STATUS_INVALID_IMAGE_FORMAT";
    case 0xC0000135: return L"STATUS_DLL_NOT_FOUND";
    case 0xC0000138: return L"STATUS_ORDINAL_NOT_FOUND";
    case 0xC0000139: return L"STATUS_ENTRYPOINT_NOT_FOUND";
    case 0xC0000142: return L"STATUS_DLL_INIT_FAILED";
    case EXCEPTION_ACCESS_VIOLATION: return L"STATUS_ACCESS_VIOLATION";
    default: return L"";
    }
}

void report_exit(DWORD code) {
    std::wprintf(L"Process exited: 0x%08lX %ls\n", code, status_name(code));
}

void report_image(const wchar_t* label, HANDLE file) {
    const WinHandle handle(file, CloseHandle);
    std::array<wchar_t, 32768> path{};
    if (handle) {
        const DWORD count = GetFinalPathNameByHandleW(handle.get(), path.data(),
            static_cast<DWORD>(path.size()), FILE_NAME_OPENED | VOLUME_NAME_DOS);
        if (count && count < path.size()) {
            std::wprintf(L"%ls: %ls\n", label, path.data());
            return;
        }
    }
    std::wprintf(L"%ls: <path unavailable>\n", label);
}

void report_debug_string(HANDLE process, const OUTPUT_DEBUG_STRING_INFO& event) {
    constexpr std::size_t limit = 4096;
    std::array<wchar_t, limit + 1> text{};
    SIZE_T read = 0;
    if (!event.nDebugStringLength) {
        return;
    }
    if (event.fUnicode) {
        const SIZE_T units = std::min<std::size_t>(event.nDebugStringLength / sizeof(wchar_t), limit);
        if (!units || !ReadProcessMemory(process, event.lpDebugStringData, text.data(),
                                        units * sizeof(wchar_t), &read)) {
            std::wprintf(L"Debug message: <unavailable>\n");
            return;
        }
    } else {
        std::array<char, limit> bytes{};
        const SIZE_T count = std::min<std::size_t>(event.nDebugStringLength, limit);
        if (!ReadProcessMemory(process, event.lpDebugStringData, bytes.data(), count, &read) ||
            !MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(read),
                                 text.data(), static_cast<int>(limit))) {
            std::wprintf(L"Debug message: <unavailable>\n");
            return;
        }
    }
    std::size_t length = std::wcslen(text.data());
    while (length && (text[length - 1] == L'\r' || text[length - 1] == L'\n')) {
        text[--length] = L'\0';
    }
    for (std::size_t i = 0; i < length; ++i) {
        if (text[i] < L' ' || text[i] == 0x7F) {
            text[i] = L' ';
        }
    }
    std::wprintf(L"Debug message: %ls\n", text.data());
}

int trace_startup(const std::filesystem::path& executable, std::wstring& command, DWORD timeout) {
    const auto directory = executable.parent_path();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    // Keep a loader failure observable even when the diagnostic runs unattended.
    const UINT old_mode = GetErrorMode();
    SetErrorMode(old_mode | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const BOOL created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
        FALSE, DEBUG_ONLY_THIS_PROCESS, nullptr, directory.c_str(), &startup, &info);
    const DWORD launch_error = created ? ERROR_SUCCESS : GetLastError();
    SetErrorMode(old_mode);
    if (!created) {
        std::fwprintf(stderr, L"CreateProcessW failed: Win32 error %lu\n", launch_error);
        return 1;
    }

    const WinHandle process(info.hProcess, CloseHandle);
    const WinHandle thread(info.hThread, CloseHandle);
    DebugSession session{info.dwProcessId};
    if (!DebugSetProcessKillOnExit(FALSE)) {
        std::fwprintf(stderr, L"DebugSetProcessKillOnExit failed: Win32 error %lu\n", GetLastError());
        return 1;
    }

    std::wprintf(L"AnyXboxSeries by edupode\nTracing PID %lu for up to %lu ms: %ls\n",
                 info.dwProcessId, timeout, executable.c_str());
    bool initial_breakpoint = true;
    const auto deadline = GetTickCount64() + timeout;
    while (true) {
        const auto now = GetTickCount64();
        if (now >= deadline) {
            break;
        }
        DEBUG_EVENT event{};
        if (!WaitForDebugEventEx(&event, static_cast<DWORD>(deadline - now))) {
            const DWORD error = GetLastError();
            if (error == ERROR_SEM_TIMEOUT || error == ERROR_TIMEOUT) {
                break;
            }
            std::fwprintf(stderr, L"WaitForDebugEventEx failed: Win32 error %lu\n", error);
            return 1;
        }

        DWORD disposition = DBG_CONTINUE;
        switch (event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            report_image(L"Process image", event.u.CreateProcessInfo.hFile);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            report_image(L"DLL loaded", event.u.LoadDll.hFile);
            break;
        case OUTPUT_DEBUG_STRING_EVENT:
            report_debug_string(process.get(), event.u.DebugString);
            break;
        case EXCEPTION_DEBUG_EVENT: {
            const auto& fault = event.u.Exception;
            const DWORD code = fault.ExceptionRecord.ExceptionCode;
            if (initial_breakpoint && code == EXCEPTION_BREAKPOINT && fault.dwFirstChance) {
                initial_breakpoint = false;
                std::wprintf(L"Initial debugger breakpoint\n");
            } else {
                disposition = DBG_EXCEPTION_NOT_HANDLED;
                std::wprintf(L"Exception: 0x%08lX %ls (%ls chance)\n", code, status_name(code),
                             fault.dwFirstChance ? L"first" : L"second");
            }
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:
            report_exit(event.u.ExitProcess.dwExitCode);
            break;
        case RIP_EVENT:
            std::wprintf(L"Debug event error: %lu (type %lu)\n",
                         event.u.RipInfo.dwError, event.u.RipInfo.dwType);
            break;
        default:
            break;
        }

        if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, disposition)) {
            std::fwprintf(stderr, L"ContinueDebugEvent failed: Win32 error %lu\n", GetLastError());
            return 1;
        }
        std::fflush(stdout);
        if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            session.pid = 0;
            return static_cast<int>(event.u.ExitProcess.dwExitCode);
        }
    }

    if (!DebugActiveProcessStop(session.pid)) {
        const DWORD error = GetLastError();
        DWORD code = 0;
        if (WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0 &&
            GetExitCodeProcess(process.get(), &code)) {
            report_exit(code);
            session.pid = 0;
            return static_cast<int>(code);
        }
        std::fwprintf(stderr, L"DebugActiveProcessStop failed: Win32 error %lu\n", error);
        return 1;
    }
    std::wprintf(L"Observation complete. Detached from PID %lu.\n", session.pid);
    session.pid = 0;
    return 0;
}
}

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);
    try {
        DWORD timeout = 15000;
        int index = 1;
        if (argc > 2 && std::wstring_view(argv[1]) == L"--observe-ms") {
            timeout = observation_time(argv[2]);
            index += 2;
        }
        if (index + 1 >= argc || std::wstring_view(argv[index]) != L"--") {
            throw std::invalid_argument("Expected an executable after --.");
        }
        const auto executable = std::filesystem::absolute(argv[++index]);
        std::wstring command;
        append_argument(command, executable.native());
        for (++index; index < argc; ++index) {
            append_argument(command, argv[index]);
        }
        if (command.size() >= 32767) {
            throw std::invalid_argument("The Windows command line is too long.");
        }
        return trace_startup(executable, command, timeout);
    } catch (const std::invalid_argument& error) {
        std::fwprintf(stderr, L"%hs\nUsage: wsx_startup_trace [--observe-ms N] -- PROGRAM.exe [ARGS...]\n",
                      error.what());
        return 2;
    } catch (const std::exception& error) {
        std::fwprintf(stderr, L"Startup trace failed: %hs\n", error.what());
        return 1;
    }
}
