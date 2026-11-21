// Copyright (c) 2026 edupode
// SPDX-License-Identifier: CPAL-1.0

#include "gameinput_devices.h"

#include <array>
#include <cstdio>

int main() {
    int first_device = 0;
    int second_device = 0;
    int third_device = 0;
    const std::array<const void*, gameinput_device_capacity> empty{};

    if (gameinput_find_device_slot(empty, &first_device) != gameinput_device_capacity) {
        std::fprintf(stderr, "An untracked device must not match an empty set.\n");
        return 1;
    }
    if (gameinput_find_empty_device_slot(empty) != 0) {
        std::fprintf(stderr, "The first free GameInput slot must be selected.\n");
        return 1;
    }

    auto devices = empty;
    auto update = gameinput_update_device(devices, &first_device, true);
    if (update.transition != GameInputDeviceTransition::Added || update.slot != 0 ||
        devices[0] != &first_device) {
        std::fprintf(stderr, "A new controller must be added to the first free slot.\n");
        return 1;
    }
    update = gameinput_update_device(devices, &first_device, true);
    if (update.transition != GameInputDeviceTransition::Unchanged || update.slot != 0) {
        std::fprintf(stderr, "A repeated connection notification must not duplicate a controller.\n");
        return 1;
    }
    update = gameinput_update_device(devices, &second_device, true);
    if (update.transition != GameInputDeviceTransition::Added || update.slot != 1 ||
        devices[1] != &second_device) {
        std::fprintf(stderr, "A second controller must use a separate slot.\n");
        return 1;
    }
    if (gameinput_find_device_slot(devices, &second_device) != 1) {
        std::fprintf(stderr, "A connected device must retain its slot.\n");
        return 1;
    }
    update = gameinput_update_device(devices, &third_device, false);
    if (update.transition != GameInputDeviceTransition::Unchanged) {
        std::fprintf(stderr, "An unknown device must not match another device.\n");
        return 1;
    }
    if (gameinput_find_empty_device_slot(devices) != 2) {
        std::fprintf(stderr, "A new controller must use the next free slot.\n");
        return 1;
    }

    update = gameinput_update_device(devices, &first_device, false);
    if (update.transition != GameInputDeviceTransition::Removed || update.slot != 0 ||
        devices[0] != nullptr) {
        std::fprintf(stderr, "Disconnecting a controller must release its slot.\n");
        return 1;
    }
    if (gameinput_find_empty_device_slot(devices) != 0) {
        std::fprintf(stderr, "Disconnecting a controller must make its slot reusable.\n");
        return 1;
    }
    update = gameinput_update_device(devices, &third_device, true);
    if (update.transition != GameInputDeviceTransition::Added || update.slot != 0) {
        std::fprintf(stderr, "A newly connected controller must reuse a disconnected slot.\n");
        return 1;
    }
    int fourth_device = 0;
    int fifth_device = 0;
    if (gameinput_update_device(devices, &fourth_device, true).transition !=
            GameInputDeviceTransition::Added ||
        gameinput_update_device(devices, &first_device, true).transition !=
            GameInputDeviceTransition::Added ||
        gameinput_update_device(devices, &fifth_device, true).transition !=
            GameInputDeviceTransition::Full) {
        std::fprintf(stderr, "The four-device capacity must be enforced.\n");
        return 1;
    }
    if (gameinput_find_empty_device_slot(devices) != gameinput_device_capacity) {
        std::fprintf(stderr, "A full controller set must report no free slot.\n");
        return 1;
    }
    return 0;
}
