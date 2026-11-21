// Copyright (c) 2026 edupode
// SPDX-License-Identifier: CPAL-1.0

#include "gameinput_input.h"

#include <cstdio>

int main() {
    constexpr auto controls = GameInputGamepadDPadLeft | GameInputGamepadDPadRight |
        GameInputGamepadDPadUp | GameInputGamepadDPadDown | GameInputGamepadA |
        GameInputGamepadB | GameInputGamepadY;
    constexpr WORD expected = XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT |
        XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_A |
        XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_Y;
    if (gameinput_buttons_to_xinput(controls) != expected) {
        std::fprintf(stderr, "GameInput button mapping differs from the expected menu controls.\n");
        return 1;
    }
    if (gameinput_buttons_to_xinput(GameInputGamepadNone) != 0) {
        std::fprintf(stderr, "An empty GameInput state must not press a menu control.\n");
        return 1;
    }
    constexpr auto unsupported = GameInputGamepadX | GameInputGamepadLeftShoulder |
        GameInputGamepadRightShoulder | GameInputGamepadLeftThumbstick |
        GameInputGamepadRightThumbstick;
    if (gameinput_buttons_to_xinput(unsupported) != 0) {
        std::fprintf(stderr, "Unmapped GameInput controls must not activate menu actions.\n");
        return 1;
    }
    if (!gameinput_is_no_reading(GAMEINPUT_E_READING_NOT_FOUND) ||
        !gameinput_is_no_reading(GAMEINPUT_E_DEVICE_NOT_FOUND) ||
        !gameinput_is_no_reading(GAMEINPUT_E_DEVICE_DISCONNECTED) ||
        !gameinput_is_no_reading(HRESULT_FROM_WIN32(ERROR_NOT_FOUND))) {
        std::fprintf(stderr, "GameInput's expected empty-device results must be recognized.\n");
        return 1;
    }
    if (gameinput_is_no_reading(S_OK) || gameinput_is_no_reading(E_FAIL)) {
        std::fprintf(stderr, "Successful and unexpected HRESULTs must not be treated as empty input.\n");
        return 1;
    }
    return 0;
}
