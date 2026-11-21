# AnyXboxSeries

![Windows x64](https://img.shields.io/static/v1?label=Platform&message=Windows%20x64&color=1f6feb)
![Experimental](https://img.shields.io/static/v1?label=Status&message=Experimental&color=d29922)

AnyXboxSeries is an experimental C++20 translation layer for Xbox Series X and S on Windows x64. It gives you a way to inspect a build, check its imports, and test a compatible module through the same Direct3D 12 loader.

GTA VI is the only game target at this stage. The project started with an Xbox build whose manifest named an executable that was missing from the copy we examined. The loader takes a build folder and a module at runtime, which leaves room to investigate other games later.

## Download for Windows

Extract the latest portable Windows x64 ZIP and double-click `Start-SignalRun.cmd` to open the sample. [Download the Windows preview](https://github.com/edupode/anyxboxseries/releases).

~~~powershell
.\bin\wsx_bootstrap.exe --sample
~~~

Signal Run is a small Windows game that tests the loader's menu, frame presentation and input. Use the arrow keys or the controller D pad to move. Enter or A confirms and Esc or B exits. Run the bootstrap without `--sample` for the standalone graphics check.

See [Build on Windows](docs/BUILD.md) to compile or contribute changes.

## Current status

The graphics probe and Signal Run have passed on an NVIDIA RTX 5070 Ti Laptop GPU and in WARP. Signal Run verifies a real DXGI swap chain and reads back pixels from its menu and game frames. The GTA VI build examined remains blocked before the game starts. It needs the title executable and support for the APIs it calls. See [Compatibility](docs/COMPATIBILITY.md).

![A verified Direct3D 12 test frame](docs/frame.png)

## What each part does

In viewer/source, Launcher.cs runs the resource viewer and PackageReader.cs reads MicrosoftGame.Config, the package manifest, and the layout files. It checks which files are present, looks for PE entry points, and opens the PNGs in the selected folder. The build.ps1 script compiles the viewer with the .NET Framework compiler.

In tools, inspect_xbox_imports.py reads regular and delay load imports with Visual Studio DUMPBIN. It checks the requested libraries against the build folder, System32, and the Windows API set contracts. With `--shims`, it also compares those imports with the export tables of candidate DLLs.

In native, bootstrap.cpp opens a DLL and passes a Direct3D 12 device and an optional build path to its WsxStartV1 entry point. The function signature lives in boot_api.h. The included implementation in boot_graphics.cpp opens the build manifest when given a path and draws the test frame.

The Direct3D bridge is implemented in d3d12_xs.cpp, with its exports declared in d3d12_xs.def. It forwards two root signature functions to the Windows runtime and records the result in shim_log.txt. graphics_runtime.h declares the device and frame operations implemented in graphics_probe.cpp. graphics_probe_main.cpp exposes those operations as a standalone test. root_signature_test.cpp checks the round trip and error cases, and CMakeLists.txt assembles the native targets and tests.

sample_game.cpp implements Signal Run as a Windows module that reuses `WsxStartV1`. It handles keyboard messages and XInput button events. window_presenter.cpp creates the DXGI swap chain, presents the frames and supports pixel readback in the tests.

## Licence and credits

AnyXboxSeries is open source under the [Common Public Attribution License 1.0](LICENSE). You may use, modify and redistribute the covered code without a licence fee under its terms.

Keep the licence and attribution notices with redistributed code. If a product includes covered code and exposes it through a graphical interface, it must prominently credit **AnyXboxSeries by edupode** as specified in Section 14 and Exhibit B. This applies to products that combine AnyXboxSeries with other code. The licence also sets source availability terms for distributing or deploying covered changes. The exact attribution is in [CREDITS.md](CREDITS.md).

## Contributing

Report the commands and results when you contribute a change, especially if it affects startup or driver selection. The branch and push workflow is in [CONTRIBUTING.md](CONTRIBUTING.md).
