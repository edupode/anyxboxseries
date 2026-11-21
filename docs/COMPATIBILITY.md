# Compatibility

AnyXboxSeries targets Windows x64. It provides a C++ translation layer prototype, a host for compatible modules, a resource viewer, and an import inspector.

## GTA VI

GTA VI is the only Xbox game target for this stage. The build examined is missing the executable declared by its manifest, so startup remains blocked. An import audit of its available DLLs matched the two Direct3D 12 imports against the existing shim. The six `xmem.dll` import records have no candidate shim.

The local Xbox package manifest declares `game_scarlett_master.exe` with target family `Scarlett`. A recursive filename inventory found no `.exe` in the package root or its `data` and `pack` folders. `Catalog000.bin` is an AMD64 PE32+ image, but DUMPBIN reports one section, zero code, entry point RVA zero, and subsystem `Xbox Code Catalog`; it is not a runnable title binary.

The local UI-resource scan finds 1,263 `RBF0` roots in `data.rpf`, including 136 `UIScreen` roots, 65 `UIMovieView` roots, and 61 `rage__fwuiFlowBlock` roots. In `script.rpf`, it finds 1,007 `PSIN v22` header candidates with an in-bounds first-section offset. These counts locate data formats but do not identify the first game screen or provide runnable game code. The archive table of contents is encrypted and the declared title executable is absent. The public PSIN format notes describe `LSNS` as high-entropy data that has not been decoded, not plaintext script code. [RBF0 format notes](https://github.com/ItsNotRexed/Leonida/blob/aae4d99e10784eb21b4f14d08470c79613b9e7fe/docs/rbf0.md) · [PSIN format notes](https://github.com/ItsNotRexed/Leonida/blob/aae4d99e10784eb21b4f14d08470c79613b9e7fe/docs/psin.md)

Run `python .\tools\inspect_ui_resources.py <data.rpf> <script.rpf>` to repeat the count. The tool reports archive offsets only when requested and does not write or export archive data.

## Windows graphics adapters

The loader uses DXGI to prefer a hardware adapter. It can use the standard adapter order when the newer preference interface is unavailable. If a hardware device cannot be created, it tries WARP. The graphics probe exposes all three paths so a driver issue can be reproduced with a small test.

~~~powershell
.\bin\wsx_graphics_probe.exe
.\bin\wsx_graphics_probe.exe --legacy-dxgi
.\bin\wsx_graphics_probe.exe --warp
~~~

The native tests verify the adapter paths by rendering a triangle and reading back the pixels. The standard adapter test exercises the fallback code on the current Windows host. Compatibility with a specific Windows build or GPU driver still depends on testing there.

The [shader probe](SHADERS.md) also compiles and dispatches SM 6.6 compute on desktop Direct3D 12. Its DXR test uses native BLAS construction, cloning, TLAS construction and inline ray queries. Xbox D3D12 device and pipeline entry points still require validation with a Scarlett executable.

## Game modules

A Windows module can receive the device and a build folder through WsxStartV1. The sample module uses that entry point to check the manifest and draw a frame. The Direct3D bridge covers two root signature calls. Xbox memory APIs and the root signature flags observed in the original console build still need a matching implementation before a game that uses them can start.

Signal Run uses the same entry point and adds Windows frame presentation and input. It reads D-pad, A, B and Y through the Windows GameInput runtime, with XInput as a fallback. Its tests check menu selection, movement, collisions, signal collection, restart and exit with pixels read back from the presented frames. A separate mapping test checks GameInput-to-menu actions, and a startup-trace test verifies that the system GameInput runtime initializes. No physical controller was connected during this verification, so a real controller event is still untested.

The Microsoft [GameInputInterfacing sample](https://github.com/microsoft/Xbox-GDK-Samples/tree/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/System/GameInputInterfacing) is an API reference. The Windows test exercises GameInput directly; it does not use or emulate the Xbox GDK runtime. A Scarlett title still requires its own `D3D12XboxCreateDevice`, `PresentX`, frame-event and runtime support, plus an authorized console executable.

## Xbox Scarlett test inputs

The 2026-11-21 workstation inventory found no GDK/GDKX installation in the standard Microsoft install directories and no authorized `Gaming.Xbox.Scarlett.x64` executable in this checkout. The available DUMPBIN is bundled with Visual Studio 18, but there is no candidate Scarlett executable to inspect. Microsoft documents the public Xbox GDK samples and their current SDK requirements in the [sample repository](https://github.com/microsoft/Xbox-GDK-Samples/). Its [SimpleTriangle sample](https://learn.microsoft.com/en-us/samples/microsoft/xbox-gdk-samples/simpletriangle/) is the first graphics/presentation reference; [GameInputInterfacing](https://learn.microsoft.com/en-us/samples/microsoft/xbox-gdk-samples/gameinputinterfacing/) selects `Gaming.Xbox.Scarlett.x64` for Xbox Series X|S. Xbox GameInput uses the runtime supplied by GDK/GameOS; desktop GameInput tests alone do not verify it ([Microsoft GameInput on Xbox](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/overviews/input-xbox?view=gdk-2610)).

To resume the Scarlett boot test, place these authorized inputs on the test workstation:

1. A usable Microsoft GDK with Xbox Extensions (GDKX) installation, its version, and its installed root. Keep the SDK outside this repository.
2. The pinned source revision and a complete `SimpleTriangle` build configured for `Gaming.Xbox.Scarlett.x64`, including its `MicrosoftGame.config` or package metadata and required runtime dependencies.
3. A matching `GameInputInterfacing` Scarlett build and the GameInput runtime/dependencies expected by that GDK release.
4. An authorized playable Scarlett sample build and its package metadata after SimpleTriangle reaches its first frame. Connect a supported controller to verify a real input event.

For each executable, run `dumpbin /headers <path-to-exe>` and `dumpbin /imports <path-to-exe>` from a Visual Studio Developer shell, then capture the first startup failure with:

~~~powershell
.\bin\wsx_startup_trace.exe --observe-ms 30000 -- '<path-to-exe>'
~~~

Record the source revision, SHA-256, target configuration, adapter/driver, commands, loaded modules, first missing import or runtime call, input event, and clean-exit result. The current Windows regression baseline is CTest 19/19 with the hardware adapter and WARP; the last recorded hardware adapter is NVIDIA GeForce RTX 5070 Ti Laptop GPU, driver 621.01. No Scarlett menu, frame, controller event, or performance result has been measured.

The separate GTA VI milestone requires a complete, authorized package containing the manifest-declared `game_scarlett_master.exe`, matching dependencies, and metadata. Asset indexes are not a substitute for the executable.
