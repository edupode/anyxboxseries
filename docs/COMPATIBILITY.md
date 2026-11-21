# Compatibility

AnyXboxSeries targets Windows x64. It provides a C++ translation layer prototype, a host for compatible modules, a resource viewer, and an import inspector.

## GTA VI

GTA VI is the only game in scope for this stage. The Xbox build examined is missing the executable declared by its manifest, so startup remains blocked. The graphics tests use a sample DLL and a scene created for this project. The loader allows other build paths and modules so the same approach can serve future titles.

## Windows graphics adapters

The loader uses DXGI to prefer a hardware adapter. It can use the standard adapter order when the newer preference interface is unavailable. If a hardware device cannot be created, it tries WARP. The graphics probe exposes all three paths so a driver issue can be reproduced with a small test.

~~~powershell
.\bin\wsx_graphics_probe.exe
.\bin\wsx_graphics_probe.exe --legacy-dxgi
.\bin\wsx_graphics_probe.exe --warp
~~~

The native tests verify the adapter paths by rendering a triangle and reading back the pixels. The standard adapter test exercises the fallback code on the current Windows host. Compatibility with a specific Windows build or GPU driver still depends on testing there.

## Game modules

A Windows module can receive the device and a build folder through WsxStartV1. The sample module uses that entry point to check the manifest and draw a frame. The Direct3D bridge covers two root signature calls. Xbox memory APIs and the root signature flags observed in the original console build still need a matching implementation before a game that uses them can start.
