# Changelog

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
