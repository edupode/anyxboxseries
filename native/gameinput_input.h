// Copyright (c) 2026 edupode
// SPDX-License-Identifier: CPAL-1.0

#pragma once

#include <GameInput.h>
#include <Xinput.h>

WORD gameinput_buttons_to_xinput(GameInputGamepadButtons buttons) noexcept;
bool gameinput_is_no_reading(HRESULT status) noexcept;
