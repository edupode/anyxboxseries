// Copyright (c) 2026 edupode
// SPDX-License-Identifier: CPAL-1.0

#include "gameinput_input.h"

WORD gameinput_buttons_to_xinput(GameInputGamepadButtons buttons) noexcept {
    struct Binding {
        GameInputGamepadButtons source;
        WORD destination;
    };
    constexpr Binding controls[]{
        {GameInputGamepadDPadLeft, XINPUT_GAMEPAD_DPAD_LEFT},
        {GameInputGamepadDPadRight, XINPUT_GAMEPAD_DPAD_RIGHT},
        {GameInputGamepadDPadUp, XINPUT_GAMEPAD_DPAD_UP},
        {GameInputGamepadDPadDown, XINPUT_GAMEPAD_DPAD_DOWN},
        {GameInputGamepadA, XINPUT_GAMEPAD_A},
        {GameInputGamepadB, XINPUT_GAMEPAD_B},
        {GameInputGamepadY, XINPUT_GAMEPAD_Y},
    };
    WORD result = 0;
    for (const auto& [source, destination] : controls) {
        if ((buttons & source) != GameInputGamepadNone) {
            result |= destination;
        }
    }
    return result;
}

bool gameinput_is_no_reading(HRESULT status) noexcept {
    return status == GAMEINPUT_E_READING_NOT_FOUND ||
        status == GAMEINPUT_E_DEVICE_NOT_FOUND ||
        status == GAMEINPUT_E_DEVICE_DISCONNECTED ||
        status == HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}
