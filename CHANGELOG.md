# Changelog

## v0.4.0-preview.4 — 2026-11-21

### Added

- Signal Run tracks GameInput gamepad connect/disconnect callbacks and combines readings from up to four connected devices. XInput remains as a fallback.
- Added a read-only RPF9 UI-resource indexer for RBF0 schema roots and PSIN v22 header candidates. It reports metadata and archive offsets only; it does not decrypt archives or execute title code.
- Added a reproducible console-test resume record covering the current SDK and executable gap.

### Verified

- Windows x64 Release build, CTest 19/19 and Python tests 13/13 on an NVIDIA GeForce RTX 5070 Ti Laptop GPU and Microsoft Basic Render Driver (WARP).
- Local GTA package inventory repeated: `data.rpf` contains 1,263 recognized RBF0 roots and `script.rpf` contains 1,007 PSIN v22 header candidates. This is an asset inventory, not a title boot.
- The Windows host read the GTA manifest and verified its own 160 × 120 diagnostic frame on the NVIDIA adapter. A separate startup-trace attempt against the manifest-declared title executable failed at `CreateProcessW` with Win32 error 2 because the file is absent.

### Known limits

- No GDKX installation was found in the standard install directories, and no authorized Scarlett sample executable was present in this checkout. The Scarlett build path and GTA VI startup remain untested.
- No physical controller was connected; GameInput hot-plug behavior is covered by the device-state test, while a real device event is still unverified.

## v0.4.0-preview.3 — 2026-11-21

### Added

- Signal Run now reads the D-pad and A/B/Y buttons through the Windows GameInput runtime. XInput remains available as a fallback.
- Added GameInput button-mapping coverage and a startup trace check for the runtime's initialized or fallback state.

### Verified

- Windows x64 Release build, CTest 18/18 and Python tests 13/13 on an NVIDIA GeForce RTX 5070 Ti Laptop GPU and Microsoft Basic Render Driver (WARP).
- The extracted portable ZIP passed its WARP self-test and startup trace. The visible menu opened, keyboard navigation started a run, movement collected a signal, and Escape exited the process cleanly.

### Known limits

- No physical controller was connected, so controller events through GameInput remain unverified.
- This is the Windows sample, not a Scarlett title build. No usable GDKX console build or authorized `game_scarlett_master.exe` was available for testing.
