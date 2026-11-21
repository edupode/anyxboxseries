# Compatibility

AnyXboxSeries targets Windows x64. It provides a C++ translation layer prototype, a host for compatible modules, a resource viewer, and an import inspector.

## GTA VI

GTA VI is the only Xbox game target for this stage. The build examined is missing the executable declared by its manifest, so startup remains blocked. An import audit of its available DLLs matched the two Direct3D 12 imports against the existing shim. The six `xmem.dll` import records have no candidate shim.

The local Xbox package manifest declares `game_scarlett_master.exe` with target family `Scarlett`. A recursive filename inventory found no `.exe` in the package root or its `data` and `pack` folders. `Catalog000.bin` is an AMD64 PE32+ image, but DUMPBIN reports one section, zero code, entry point RVA zero, and subsystem `Xbox Code Catalog`; it is not a runnable title binary.

The package also contains `gameos.xvd` (348,069,888 bytes, about 331.5 MiB), but no `.xvc`. Microsoft documents `GameOs.xvd` as the Game OS image that MakePkg embeds automatically into an Xbox package; this is the console runtime image, not the manifest-declared GTA title executable. The local folder therefore still has no title entry point. [GDK deployment schema](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/packaging/deployment/atoc-deployment-schema?view=gdk-2510) · [GDK packaging overview](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/packaging/overviews/packaging?view=gdk-2604)

## GTA startup attempts

On 2026-11-21, the portable Windows host was pointed at the local Xbox package with `wsx_bootstrap.exe --build <package-root>`. On the NVIDIA GeForce RTX 5070 Ti Laptop GPU (driver 621.01), it read `MicrosoftGame.Config`, created a desktop D3D12 device, and verified its own 160 × 120 diagnostic triangle frame (5,040 triangle pixels and 14,160 background pixels, exit 0). This proves manifest access and the Windows renderer path only; it does not load the game's executable or render a GTA screen.

The next attempt used `wsx_startup_trace.exe --observe-ms 30000 -- <package-root>\\game_scarlett_master.exe`. `CreateProcessW` failed immediately with Win32 error 2 (`ERROR_FILE_NOT_FOUND`); no title process was created, so there were no title imports or runtime calls to prioritize. This is the first reproducible startup boundary for the supplied package.

The supplied [Xbox2Windows](https://github.com/R0Y00/Xbox2Windows/tree/b53bfb0b6489398aba7a81f3761e2d56aaa00ccf) repository is a useful reference for static translation of actual PE functions into Windows code and for checking a manifest-declared executable's entry point, imports, TLS callbacks, and startup ranges. Its README explicitly leaves title CPU execution and full-game startup unverified; its intake tool inspects a candidate PE that already exists. It does not generate the missing game executable from RPF assets. The repository is MIT-licensed; this project has not copied its source.

The public [ScarlettX](https://github.com/Freminet/ScarlettX) project offers a useful comparison for a PE64 loader and Xbox runtime shims, but its README says encrypted retail XVC/XVD support is unimplemented and its tests use synthetic PE titles. It still needs a runnable title image and does not reconstruct a missing game entry point. No ScarlettX code was copied; no license file was present in the inspected repository root.

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

The Microsoft samples are pinned to [`Xbox-GDK-Samples` revision `b1e77b6ac10f95b07296025fda171ca2ddc42dbe](https://github.com/microsoft/Xbox-GDK-Samples/tree/b1e77b6ac10f95b07296025fda171ca2ddc42dbe), commit time `2026-11-06T18:17:46Z`. At that revision, SimpleTriangle is `Samples/IntroGraphics/SimpleTriangle/SimpleTriangle.vcxproj`; its `Release|Gaming.Xbox.Scarlett.x64` configuration is an Application project, so `SimpleTriangle.exe` is the expected output name (not built on this workstation). The Xbox path calls `D3D12XboxCreateDevice`, configures `SetFrameIntervalX` and `ScheduleFrameEventX`, waits with `WaitFrameEventX`, and presents with `ID3D12CommandQueue::PresentX` ([source](https://github.com/microsoft/Xbox-GDK-Samples/blob/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/IntroGraphics/SimpleTriangle/DeviceResources.cpp)). This is the device/frame milestone; SimpleTriangle only has an exit control, so it does not satisfy the menu or controller criteria.

The current `d3d12_xs.dll` definition exports only `D3D12CreateRootSignatureDeserializer` and `D3D12SerializeVersionedRootSignature`; it does not export `D3D12XboxCreateDevice`. That makes device creation the first source-derived missing-symbol candidate, not a measured import failure: the sample EXE is absent, so its actual import DLL/name table cannot be checked with DUMPBIN. After device creation, `PresentX`, `WaitFrameEventX`, `SetFrameIntervalX`, and `ScheduleFrameEventX` also need compatible Xbox COM behavior; exporting one function alone would not complete the sample path.

The same revision's GameInput reference is `Samples/System/GameInputInterfacing/GameInputInterfacing.vcxproj`, with a `Release|Gaming.Xbox.Scarlett.x64` configuration and expected output `GameInputInterfacing.exe` (also not built here). The Windows test exercises the Windows GameInput runtime directly; it does not use or emulate the Xbox GDK runtime. A playable Scarlett sample is still needed after the graphics and GameInput references.

## Xbox Scarlett test inputs

The 2026-11-21 workstation inventory found no GDK/GDKX installation in the standard Microsoft install directories and no authorized `Gaming.Xbox.Scarlett.x64` executable in this checkout. The available DUMPBIN is bundled with Visual Studio 18, but there is no candidate Scarlett executable to inspect. Microsoft documents the public Xbox GDK samples and their current SDK requirements in the [sample repository](https://github.com/microsoft/Xbox-GDK-Samples/). The [SimpleTriangle sample](https://learn.microsoft.com/en-us/samples/microsoft/xbox-gdk-samples/simpletriangle/) and [GameInputInterfacing](https://learn.microsoft.com/en-us/samples/microsoft/xbox-gdk-samples/gameinputinterfacing/) select `Gaming.Xbox.Scarlett.x64` for Xbox Series X|S. Xbox GameInput uses the runtime supplied by GDK/GameOS; desktop GameInput tests alone do not verify it ([Microsoft GameInput on Xbox](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/overviews/input-xbox?view=gdk-2610)).

To resume the Scarlett boot test, place these authorized inputs on the test workstation:

1. A usable Microsoft GDK with Xbox Extensions (GDKX) installation at version 2510 or later, its installed root, and authorized devkit/sandbox access. Keep the SDK outside this repository.
2. A complete `SimpleTriangle` build from revision `b1e77b6ac10f95b07296025fda171ca2ddc42dbe`, configured for `Gaming.Xbox.Scarlett.x64`, including `SimpleTriangle.exe`, its `MicrosoftGame.config` or package metadata, and required runtime dependencies.
3. A matching `GameInputInterfacing.exe` Scarlett build from the same revision and the GameInput runtime/dependencies expected by that GDK release.
4. An authorized playable Scarlett sample build and its package metadata after SimpleTriangle reaches its first frame. Connect a supported controller to verify a real input event.

For each executable, run `dumpbin /headers <path-to-exe>` and `dumpbin /imports <path-to-exe>` from a Visual Studio Developer shell, then capture the first startup failure with:

~~~powershell
.\bin\wsx_startup_trace.exe --observe-ms 30000 -- '<path-to-exe>'
~~~

No `dumpbin` or startup-trace command has been run for these Microsoft samples because their Scarlett executables are not present; there is no sample launch result or console adapter to report. The desktop regression baseline below is from AnyXboxSeries, not from SimpleTriangle.

Record the source revision, SHA-256, target configuration, adapter/driver, commands, loaded modules, first missing import or runtime call, input event, and clean-exit result. The desktop regression was rerun on 2026-11-21 at source commit `5498c0bbaeb59f434e55578afd9932cd8fc9047a`, target Windows x64: Visual Studio 18 `ctest.exe --test-dir build -C Release --output-on-failure` passed 19/19, and `python -m unittest discover -s tools` passed 13/13. CTest exercised the NVIDIA GeForce RTX 5070 Ti Laptop GPU (driver `32.0.16.2101`) and Microsoft Basic Render Driver (WARP), including Signal Run and DXGI frame readback. These are AnyXboxSeries desktop tests, not a Scarlett sample run. No Scarlett menu, frame, controller event, or performance result has been measured.

The separate GTA VI milestone requires a complete, authorized package containing the manifest-declared `game_scarlett_master.exe`, matching dependencies, and metadata. Asset indexes are not a substitute for the executable.
