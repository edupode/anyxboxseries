// Copyright (c) 2026 edupode
// SPDX-License-Identifier: CPAL-1.0

#pragma once

#include <array>
#include <cstddef>

inline constexpr std::size_t gameinput_device_capacity = 4;
using GameInputDeviceIdentities = std::array<const void*, gameinput_device_capacity>;

enum class GameInputDeviceTransition {
    Unchanged,
    Added,
    Removed,
    Full,
};

struct GameInputDeviceUpdate {
    GameInputDeviceTransition transition = GameInputDeviceTransition::Unchanged;
    std::size_t slot = gameinput_device_capacity;
};

inline std::size_t gameinput_find_device_slot(
    const GameInputDeviceIdentities& devices, const void* device) noexcept {
    for (std::size_t index = 0; index < devices.size(); ++index) {
        if (devices[index] == device) {
            return index;
        }
    }
    return devices.size();
}

inline std::size_t gameinput_find_empty_device_slot(
    const GameInputDeviceIdentities& devices) noexcept {
    return gameinput_find_device_slot(devices, nullptr);
}

inline GameInputDeviceUpdate gameinput_update_device(
    GameInputDeviceIdentities& devices, const void* device, bool connected) noexcept {
    if (!device) {
        return {};
    }

    const std::size_t existing = gameinput_find_device_slot(devices, device);
    if (connected) {
        if (existing != devices.size()) {
            return {GameInputDeviceTransition::Unchanged, existing};
        }
        const std::size_t empty = gameinput_find_empty_device_slot(devices);
        if (empty == devices.size()) {
            return {GameInputDeviceTransition::Full, empty};
        }
        devices[empty] = device;
        return {GameInputDeviceTransition::Added, empty};
    }

    if (existing == devices.size()) {
        return {};
    }
    devices[existing] = nullptr;
    return {GameInputDeviceTransition::Removed, existing};
}
