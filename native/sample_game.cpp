// Copyright (c) 2026 edupode
// SPDX-License-Identifier: CPAL-1.0
//
// The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
// Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
// The Original Code is AnyXboxSeries.
// The Original Developer is the Initial Developer.
// The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
//

#include "boot_api.h"
#include "gameinput_input.h"
#include "window_presenter.h"

#include <Xinput.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {
using wsx::FrameColor;
using wsx::FrameRectangle;
using wsx::PixelReadback;
using Color = std::array<std::uint8_t, 4>;
using Scene = std::vector<FrameRectangle>;

constexpr wchar_t class_name[] = L"AnyXboxSeries.SignalRun";
constexpr LONG frame_width = 640;
constexpr LONG frame_height = 448;
constexpr LONG board_left = 56;
constexpr LONG board_top = 108;
constexpr LONG column_step = 64;
constexpr LONG row_step = 54;
constexpr LONG columns = 8;
constexpr LONG rows = 5;

constexpr Color background{8, 15, 28, 255};
constexpr Color panel{16, 27, 45, 255};
constexpr Color tile{23, 42, 62, 255};
constexpr Color wall{51, 67, 88, 255};
constexpr Color text{229, 239, 249, 255};
constexpr Color muted{141, 165, 189, 255};
constexpr Color player{63, 222, 202, 255};
constexpr Color signal{254, 193, 104, 255};

struct Cell {
    LONG column;
    LONG row;
    bool operator==(const Cell&) const = default;
};

constexpr Cell starting_cell{0, 2};
constexpr std::array walls{Cell{1, 1}, Cell{2, 2}, Cell{2, 3},
                           Cell{4, 1}, Cell{4, 3}, Cell{6, 2}};
constexpr std::array signals{Cell{1, 2}, Cell{3, 0}, Cell{7, 4}};

enum class Screen { Menu, Playing, Cleared };

struct GameState {
    Screen screen = Screen::Menu;
    Cell rover = starting_cell;
    std::array<bool, signals.size()> collected{};
    std::size_t score = 0;
    bool exit_selected = false;
    bool exiting = false;

    void start() noexcept {
        screen = Screen::Playing;
        rover = starting_cell;
        collected.fill(false);
        score = 0;
    }

    void move(LONG horizontal, LONG vertical) noexcept {
        const Cell next{rover.column + horizontal, rover.row + vertical};
        if (next.column < 0 || next.column >= columns || next.row < 0 || next.row >= rows ||
            std::ranges::find(walls, next) != walls.end()) {
            return;
        }
        rover = next;
        for (std::size_t index = 0; index < signals.size(); ++index) {
            if (rover == signals[index] && !collected[index]) {
                collected[index] = true;
                if (++score == signals.size()) {
                    screen = Screen::Cleared;
                }
                break;
            }
        }
    }

    void key(WPARAM key_code) noexcept {
        if (key_code == VK_ESCAPE) {
            exiting = true;
            return;
        }
        if (screen == Screen::Menu) {
            if (key_code == VK_UP || key_code == VK_DOWN) {
                exit_selected = !exit_selected;
            } else if (key_code == VK_RETURN) {
                if (exit_selected) {
                    exiting = true;
                } else {
                    start();
                }
            }
        } else if (screen == Screen::Cleared) {
            if (key_code == VK_RETURN || key_code == 'R') {
                start();
            }
        } else {
            switch (key_code) {
                case VK_LEFT: move(-1, 0); break;
                case VK_RIGHT: move(1, 0); break;
                case VK_UP: move(0, -1); break;
                case VK_DOWN: move(0, 1); break;
                case 'R': start(); break;
            }
        }
    }
};

FrameColor frame_color(const Color& color) noexcept {
    return {color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, color[3] / 255.0f};
}

void rectangle(Scene& scene, LONG x, LONG y, LONG width, LONG height, const Color& color) {
    scene.push_back({{x, y, x + width, y + height}, frame_color(color)});
}

using Glyph = std::array<std::uint8_t, 7>;
constexpr std::array<Glyph, 26> letters{{
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
    {14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30},
    {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17},
    {31, 4, 4, 4, 4, 4, 31}, {7, 2, 2, 2, 18, 18, 12},
    {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17},
    {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16},
    {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30}, {31, 4, 4, 4, 4, 4, 4},
    {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},
    {17, 17, 17, 21, 21, 27, 17}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4}, {31, 1, 2, 4, 8, 16, 31}
}};
constexpr std::array<Glyph, 10> digits{{
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
    {14, 17, 1, 2, 4, 8, 31}, {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2}, {31, 16, 16, 30, 1, 1, 30},
    {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14}
}};

Glyph glyph(char character) noexcept {
    if (character >= 'A' && character <= 'Z') {
        return letters[static_cast<std::size_t>(character - 'A')];
    }
    if (character >= '0' && character <= '9') {
        return digits[static_cast<std::size_t>(character - '0')];
    }
    switch (character) {
        case '/': return {1, 1, 2, 4, 8, 16, 16};
        case '.': return {0, 0, 0, 0, 0, 4, 4};
        case ':': return {0, 0, 4, 4, 0, 4, 4};
        case '(': return {2, 4, 8, 8, 8, 4, 2};
        case ')': return {8, 4, 2, 2, 2, 4, 8};
        default: return {};
    }
}

void caption(Scene& scene, LONG x, LONG y, LONG scale, const Color& color, std::string_view label) {
    for (char character : label) {
        const auto pixels = glyph(character);
        for (LONG row = 0; row < static_cast<LONG>(pixels.size()); ++row) {
            for (LONG column = 0; column < 5; ++column) {
                if ((pixels[static_cast<std::size_t>(row)] & (1 << (4 - column))) != 0) {
                    rectangle(scene, x + column * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        x += 6 * scale;
    }
}

void centered(Scene& scene, LONG y, LONG scale, const Color& color, std::string_view label) {
    const LONG width = (6 * static_cast<LONG>(label.size()) - 1) * scale;
    caption(scene, (frame_width - width) / 2, y, scale, color, label);
}

void menu_button(Scene& scene, LONG y, bool selected, std::string_view label) {
    rectangle(scene, 86, y, 468, 50, selected ? player : panel);
    centered(scene, y + 15, 3, selected ? background : text, label);
}

void make_scene(const GameState& game, Scene& scene) {
    scene.clear();
    if (game.screen == Screen::Menu) {
        centered(scene, 50, 7, text, "SIGNAL RUN");
        centered(scene, 126, 2, muted, "RECOVER THREE SIGNALS");
        menu_button(scene, 182, !game.exit_selected, "START RUN");
        menu_button(scene, 247, game.exit_selected, "EXIT");
        centered(scene, 333, 2, muted, "ARROWS OR D PAD TO SELECT");
        centered(scene, 357, 2, muted, "ENTER OR A TO CONFIRM");
    } else if (game.screen == Screen::Cleared) {
        rectangle(scene, 86, 111, 468, 146, panel);
        rectangle(scene, 86, 111, 468, 4, signal);
        centered(scene, 151, 4, signal, "ALL SIGNALS FOUND");
        centered(scene, 210, 2, text, "ENTER OR A TO PLAY AGAIN");
        centered(scene, 281, 2, muted, "ESC OR B TO EXIT");
    } else {
        caption(scene, 56, 35, 4, text, "SIGNAL RUN");
        const char score[]{static_cast<char>('0' + game.score), '/', '3'};
        caption(scene, 496, 52, 3, signal, {score, sizeof(score)});
        for (LONG row = 0; row < rows; ++row) {
            for (LONG column = 0; column < columns; ++column) {
                const Cell cell{column, row};
                const LONG x = board_left + column * column_step;
                const LONG y = board_top + row * row_step;
                const bool blocked = std::ranges::find(walls, cell) != walls.end();
                rectangle(scene, x, y, 58, 48, blocked ? wall : tile);
                for (std::size_t index = 0; index < signals.size(); ++index) {
                    if (cell == signals[index] && !game.collected[index]) {
                        rectangle(scene, x + 21, y + 15, 16, 18, signal);
                    }
                }
                if (cell == game.rover) {
                    rectangle(scene, x + 11, y + 9, 36, 30, player);
                    rectangle(scene, x + 18, y + 15, 7, 6, background);
                    rectangle(scene, x + 33, y + 15, 7, 6, background);
                }
            }
        }
        centered(scene, 386, 2, muted, "MOVE WITH ARROWS OR D PAD");
    }
    centered(scene, 410, 2, muted, "ANYXBOXSERIES BY EDUPODE   COPYRIGHT (C) 2026 EDUPODE");
    centered(scene, 429, 2, muted, "HTTPS://GITHUB.COM/EDUPODE/ANYXBOXSERIES");
}

void gamepad_keys(GameState& game, WORD pressed) noexcept {
    struct Binding {
        WORD button;
        WPARAM key;
    };
    constexpr Binding controls[]{
        {XINPUT_GAMEPAD_DPAD_LEFT, VK_LEFT}, {XINPUT_GAMEPAD_DPAD_RIGHT, VK_RIGHT},
        {XINPUT_GAMEPAD_DPAD_UP, VK_UP}, {XINPUT_GAMEPAD_DPAD_DOWN, VK_DOWN},
        {XINPUT_GAMEPAD_A, VK_RETURN}, {XINPUT_GAMEPAD_B, VK_ESCAPE}, {XINPUT_GAMEPAD_Y, 'R'}
    };
    for (const auto& [button, key] : controls) {
        if ((pressed & button) != 0) {
            game.key(key);
        }
    }
}

class GameInputRuntime {
public:
    GameInputRuntime() noexcept {
        module_ = LoadLibraryExW(L"GameInput.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module_) {
            report("GameInput runtime unavailable; XInput fallback remains active.\n");
            return;
        }

        using CreateFunction = HRESULT(WINAPI*)(IGameInput**);
        const auto create = reinterpret_cast<CreateFunction>(GetProcAddress(module_, "GameInputCreate"));
        if (!create) {
            report("GameInputCreate is unavailable; XInput fallback remains active.\n");
            return;
        }

        IGameInput* game_input = nullptr;
        const HRESULT status = create(&game_input);
        if (FAILED(status) || !game_input) {
            char message[128]{};
            std::snprintf(message, sizeof(message),
                          "GameInputCreate failed with HRESULT 0x%08lX; XInput fallback remains active.\n",
                          static_cast<unsigned long>(status));
            report(message);
            if (game_input) {
                game_input->Release();
            }
            return;
        }
        game_input_.Attach(game_input);
        report("GameInput runtime initialized; XInput fallback remains active.\n");
    }

    ~GameInputRuntime() {
        game_input_.Reset();
        if (module_) {
            FreeLibrary(module_);
        }
    }

    GameInputRuntime(const GameInputRuntime&) = delete;
    GameInputRuntime& operator=(const GameInputRuntime&) = delete;

    bool available() const noexcept {
        return game_input_.Get() != nullptr;
    }

    bool current_gamepad_buttons(WORD& buttons) noexcept {
        buttons = 0;
        if (!game_input_) {
            return false;
        }

        Microsoft::WRL::ComPtr<IGameInputReading> reading;
        const HRESULT status = game_input_->GetCurrentReading(
            GameInputKindGamepad, nullptr, reading.GetAddressOf());
        if (gameinput_is_no_reading(status)) {
            last_read_status_ = S_OK;
            return false;
        }
        if (FAILED(status) || !reading) {
            if (status != last_read_status_) {
                char message[128]{};
                std::snprintf(message, sizeof(message),
                              "GameInput GetCurrentReading failed with HRESULT 0x%08lX.\n",
                              static_cast<unsigned long>(status));
                report(message);
                last_read_status_ = status;
            }
            return false;
        }
        last_read_status_ = S_OK;

        GameInputGamepadState state{};
        if (!reading->GetGamepadState(&state)) {
            return false;
        }
        buttons = gameinput_buttons_to_xinput(state.buttons);
        return true;
    }

private:
    static void report(const char* message) noexcept {
        std::fputs(message, stdout);
        OutputDebugStringA(message);
    }

    HMODULE module_ = nullptr;
    Microsoft::WRL::ComPtr<IGameInput> game_input_;
    HRESULT last_read_status_ = S_OK;
};

WORD connected_gamepad_buttons() noexcept {
    XINPUT_STATE input{};
    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
        if (XInputGetState(index, &input) == ERROR_SUCCESS) {
            return input.Gamepad.wButtons;
        }
    }
    return 0;
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
    }
    auto* game = reinterpret_cast<GameState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (game && message == WM_CLOSE) {
        game->exiting = true;
        return 0;
    }
    if (game && message == WM_KEYDOWN) {
        if ((static_cast<ULONG_PTR>(lparam) & (1UL << 30)) == 0) {
            game->key(wparam);
        }
        return 0;
    }
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

struct WindowClass {
    HINSTANCE instance;
    ~WindowClass() { UnregisterClassW(class_name, instance); }
};

HRESULT test_failure(const char* step) noexcept {
    std::fprintf(stderr, "Signal Run self test failed at %s\n", step);
    return E_FAIL;
}

bool check_frame(wsx::WindowPresenter& presenter, const GameState& game, Scene& scene,
                 PixelReadback pixel, const Color& expected) {
    make_scene(game, scene);
    if (FAILED(presenter.draw(frame_color(background), scene, &pixel))) {
        return false;
    }
    if (pixel.rgba != expected) {
        std::fprintf(stderr, "Pixel at (%u, %u) was %u,%u,%u,%u, expected %u,%u,%u,%u\n",
                     pixel.x, pixel.y, pixel.rgba[0], pixel.rgba[1], pixel.rgba[2], pixel.rgba[3],
                     expected[0], expected[1], expected[2], expected[3]);
        return false;
    }
    return true;
}

HRESULT test_game(HWND window, wsx::WindowPresenter& presenter, GameState& game, Scene& scene) {
    if (!check_frame(presenter, game, scene, {90, 190}, player)) {
        return test_failure("initial menu frame");
    }

    SendMessageW(window, WM_KEYDOWN, VK_DOWN, 0);
    if (!game.exit_selected || !check_frame(presenter, game, scene, {90, 255}, player)) {
        return test_failure("keyboard menu navigation");
    }
    SendMessageW(window, WM_KEYDOWN, VK_UP, 0);
    gamepad_keys(game, XINPUT_GAMEPAD_A);
    if (game.screen != Screen::Playing || game.rover != starting_cell) {
        return test_failure("gamepad confirmation");
    }

    SendMessageW(window, WM_KEYDOWN, VK_RIGHT, 0);
    SendMessageW(window, WM_KEYDOWN, VK_RIGHT, 0);
    if (game.rover != Cell{1, 2} || game.score != 1 ||
        !check_frame(presenter, game, scene, {148, 240}, player)) {
        return test_failure("movement, wall collision and signal pickup");
    }

    constexpr std::array<WPARAM, 16> route{
        VK_DOWN, VK_DOWN, VK_RIGHT, VK_RIGHT, VK_UP, VK_UP, VK_UP, VK_UP,
        VK_RIGHT, VK_RIGHT, VK_RIGHT, VK_RIGHT, VK_DOWN, VK_DOWN, VK_DOWN, VK_DOWN
    };
    for (const auto key : route) {
        SendMessageW(window, WM_KEYDOWN, key, 0);
    }
    if (game.screen != Screen::Cleared || game.score != signals.size() ||
        !check_frame(presenter, game, scene, {90, 112}, signal)) {
        return test_failure("complete the run and render the result screen");
    }

    SendMessageW(window, WM_KEYDOWN, VK_RETURN, 0);
    if (game.screen != Screen::Playing || game.score != 0 || game.rover != starting_cell) {
        return test_failure("restart the game");
    }
    gamepad_keys(game, XINPUT_GAMEPAD_B);
    if (!game.exiting) {
        return test_failure("gamepad exit");
    }

    std::puts("Signal Run verified menu, movement, collision, all signals, restart, button mapping and exit.");
    std::puts("Four Direct3D 12 swap chain frames passed pixel readback.");
    return S_OK;
}

HRESULT run_game(ID3D12Device& device, bool self_test) {
    GameState game;
    GameInputRuntime game_input;
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = &window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = class_name;
    if (!RegisterClassW(&window_class)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const WindowClass registered_class{instance};

    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT window_bounds{0, 0, frame_width, frame_height};
    if (!AdjustWindowRect(&window_bounds, style, FALSE)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)> window(
        CreateWindowExW(0, class_name, L"Signal Run | AnyXboxSeries by edupode", style,
                        CW_USEDEFAULT, CW_USEDEFAULT,
                        window_bounds.right - window_bounds.left,
                        window_bounds.bottom - window_bounds.top,
                        nullptr, nullptr, instance, &game),
        DestroyWindow);
    if (!window) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    wsx::WindowPresenter presenter;
    HRESULT status = presenter.initialize(device, window.get(), frame_width, frame_height, self_test);
    if (FAILED(status)) {
        return status;
    }
    Scene scene;
    scene.reserve(2048);
    if (self_test) {
        WORD buttons = 0;
        if (game_input.current_gamepad_buttons(buttons)) {
            std::puts("GameInput returned a gamepad reading during the self test.");
        } else if (game_input.available()) {
            std::puts("GameInput reading path ready; no current gamepad reading.");
        }
        return test_game(window.get(), presenter, game, scene);
    }

    ShowWindow(window.get(), SW_SHOWNORMAL);
    WORD previous_buttons = 0;
    if (!game_input.current_gamepad_buttons(previous_buttons)) {
        previous_buttons = connected_gamepad_buttons();
    }
    while (!game.exiting) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(message.wParam));
                return S_OK;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (game.exiting) {
            break;
        }
        if (IsIconic(window.get())) {
            WaitMessage();
            continue;
        }
        WORD buttons = 0;
        if (!game_input.current_gamepad_buttons(buttons)) {
            buttons = connected_gamepad_buttons();
        }
        if (GetForegroundWindow() == window.get()) {
            const auto pressed = static_cast<WORD>(buttons & ~previous_buttons);
            gamepad_keys(game, pressed);
        }
        previous_buttons = buttons;
        if (game.exiting) {
            break;
        }
        make_scene(game, scene);
        status = presenter.draw(frame_color(background), scene);
        if (FAILED(status)) {
            return status;
        }
    }
    return S_OK;
}
}

extern "C" __declspec(dllexport) HRESULT WINAPI WsxStartV1(ID3D12Device* device, LPCWSTR) noexcept {
    if (!device) {
        return E_POINTER;
    }
    try {
        wchar_t mode[2]{};
        const bool self_test = GetEnvironmentVariableW(L"WSX_SAMPLE_SELF_TEST", mode, 2) == 1 &&
                               mode[0] == L'1';
        return run_game(*device, self_test);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Signal Run: %s\n", error.what());
        return E_FAIL;
    }
}
