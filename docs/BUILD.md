# Build on Windows

Open the x64 Visual Studio development console with CMake, Ninja, and a Windows SDK that includes the root signature 1.2 declarations. Run these commands from the repository root.

~~~powershell
cmake -S native -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix .
.\Start-SignalRun.cmd
.\bin\wsx_bootstrap.exe
~~~

You can choose a build folder and a compatible module at startup. The probe can use the standard DXGI adapter order, render using WARP, or save a frame as PPM.

~~~powershell
.\bin\wsx_bootstrap.exe --build 'C:\Games\SampleBuild'
.\bin\wsx_bootstrap.exe --sample
.\bin\wsx_bootstrap.exe --sample --warp
.\bin\wsx_graphics_probe.exe --legacy-dxgi
.\bin\wsx_graphics_probe.exe --warp
.\bin\wsx_graphics_probe.exe --frame .\frame.ppm
.\viewer\source\build.ps1
.\viewer\WSX-Resource-Launcher.exe 'C:\Games\SampleBuild'
python .\tools\inspect_xbox_imports.py 'C:\Games\SampleBuild' --shims .\bin --output .\imports.txt
python -m unittest discover -s tools
~~~

The viewer uses WinForms on .NET Framework. The import inspector requires Python 3.10 or newer and DUMPBIN.

The import inspector selects the launch executable in `MicrosoftGame.Config` or `AppxManifest.xml`. Supply its name explicitly for a folder with multiple entries. A manifest-selected executable must be present. For an explicit name, use `--require-entry` to make a missing file fail the command. The report records the AMD64 entry point, SHA-256, and the first visible gap in the executable's direct regular imports.

Add `-DWSX_DXC_DIR="<x64 DXC directory>"` to the first CMake command to enable the tests for SM 6.6, FSSM translation and DXR 1.1. The corresponding SDK headers include `dxcapi.h`. See the [shader guide](SHADERS.md) for runtime requirements and commands.

CTest runs Signal Run with the preferred adapter and with WARP. The self test sends keyboard events to the game window, checks button mapping, completes a run and verifies pixels from four frames.
