# Compatibility

AnyXboxSeries targets Windows x64. It provides a C++ translation layer prototype, a host for compatible modules, a resource viewer, and an import inspector.

## GTA VI

GTA VI is the only Xbox game target for this stage. The build examined is missing the executable declared by its manifest, so startup remains blocked. An import audit of its available DLLs matched the two Direct3D 12 imports against the existing shim. The six `xmem.dll` import records have no candidate shim.

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
