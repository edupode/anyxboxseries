# Build on Windows

Open the x64 Visual Studio development console with CMake, Ninja, and a Windows SDK that includes the root signature 1.2 declarations. Run these commands from the repository root.

~~~powershell
cmake -S native -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix .
.\bin\wsx_bootstrap.exe
~~~

You can choose a build folder and a compatible module at startup. The probe can use the standard DXGI adapter order, render using WARP, or save a frame as PPM.

~~~powershell
.\bin\wsx_bootstrap.exe --build 'C:\Games\SampleBuild'
.\bin\wsx_graphics_probe.exe --legacy-dxgi
.\bin\wsx_graphics_probe.exe --warp
.\bin\wsx_graphics_probe.exe --frame .\frame.ppm
.\viewer\source\build.ps1
.\viewer\WSX-Resource-Launcher.exe 'C:\Games\SampleBuild'
python .\tools\inspect_xbox_imports.py 'C:\Games\SampleBuild' 'game.exe' --output .\imports.txt
~~~

The viewer uses WinForms on .NET Framework. The import inspector requires Python 3.10 or newer and DUMPBIN.
