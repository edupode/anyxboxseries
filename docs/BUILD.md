# Build on Windows

Build with MSVC x64 and a Windows SDK that includes the root signature 1.2 declarations. This project is verified with the Visual Studio 18 2026 CMake generator; choose the matching Visual Studio generator installed on your machine.

~~~powershell
cmake -S native -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix .
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
.\bin\wsx_graphics_probe.exe --present
.\bin\wsx_graphics_probe.exe --warp --present
.\bin\wsx_graphics_probe.exe --frame .\frame.ppm
.\bin\wsx_startup_trace.exe --observe-ms 30000 -- 'C:\Games\Authorized Build\game_scarlett_master.exe'
.\viewer\source\build.ps1
.\viewer\WSX-Resource-Launcher.exe 'C:\Games\SampleBuild'
python .\tools\inspect_xbox_imports.py 'C:\Games\SampleBuild' --shims .\bin --output .\imports.txt
python .\tools\inspect_ui_resources.py 'C:\Games\SampleBuild\data.rpf' 'C:\Games\SampleBuild\script.rpf'
python .\tools\rbf0_tree.py 'C:\Games\SampleBuild\data.rpf' --find STACK_Main_Menu
python .\tools\rbf0_tree.py 'C:\Games\SampleBuild\data.rpf' --offset 0x11D3C000 --dump-strings
python -m unittest discover -s tools
~~~

The viewer uses WinForms on .NET Framework. The import inspector requires Python 3.10 or newer and DUMPBIN. Signal Run loads the Windows GameInput runtime from System32 at startup; if it is unavailable, the sample keeps its XInput fallback.

If Cargo for `x86_64-pc-windows-msvc` is installed, CMake also builds the separate TomBox XMem candidate at `build/candidates/tombox-xmem/xmem.dll` and enables its Rust source tests and Windows smoke test. The Rust tests run serially because they share simulated physical-memory state. Without the matching Cargo toolchain, the regular C++ build and test suite continue to work, and the existing Xbox2Windows XMem shim remains the default provider. The candidate's upstream MIT notice is included with portable installs.

`--present` creates a short-lived window, presents one triangle through a DXGI swap chain, and checks the rendered pixels. `--warp --present` runs the same check on WARP. `wsx_startup_trace` launches the specified executable, reports loaded images, debug strings, exceptions, and its exit code, then detaches after the observation limit if it is still running. It does not modify the executable or its files. Run it only with a build you are authorized to launch.

The import inspector selects the launch executable in `MicrosoftGame.Config` or `AppxManifest.xml`. Supply its name explicitly for a folder with multiple entries. A manifest-selected executable must be present. For an explicit name, use `--require-entry` to make a missing file fail the command. The report records the AMD64 entry point, SHA-256, and the first visible gap in the executable's direct regular imports.

`rbf0_tree.py` is a read-only parser for the RBF0 trees found in readable RPF9 zlib streams. A full scan reports stream, resource, and record counts; `--find` searches string, descriptor, and text values. `--offset` selects one compressed resource, and `--dump-strings` prints its values. The sample offset above is from the examined local `data.rpf`; offsets vary between package builds.

Add `-DWSX_DXC_DIR="<x64 DXC directory>"` to the first CMake command to enable the tests for SM 6.6, FSSM translation and DXR 1.1. The corresponding SDK headers include `dxcapi.h`. See the [shader guide](SHADERS.md) for runtime requirements and commands.

CTest runs Signal Run with the preferred adapter and with WARP. Its self test sends keyboard events to the game window, checks GameInput-to-menu button mapping, completes a run and verifies pixels from four frames. A separate test launches the sample through `wsx_startup_trace`, checks that GameInput reports its initialized or fallback state, and verifies the child exits. No physical controller is required for these tests. The suite also checks a DXGI swap-chain present on both adapters and a graphics-probe process exit.
