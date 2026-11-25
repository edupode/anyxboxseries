# Objetivo do AnyXboxSeries — menu original do GTA VI

~~~text
/goal Criar um iniciador próprio para Windows x64 que, a partir dos ficheiros locais do GTA VI 1.0.2.1 e sem carregar nem invocar game_scarlett_master.exe, consiga chegar ao menu principal original do jogo. Considerar o objetivo cumprido quando for possível repetir o arranque do zero, navegar no menu e ativar uma opção com teclado ou comando, mesmo a poucos FPS. Demonstrar quais os ficheiros originais que definem o ecrã e a interação observada.
~~~

O primeiro passo é identificar nos ficheiros disponíveis as definições de interface, os recursos visuais e os scripts que possam descrever o menu. Ainda está por determinar onde reside a lógica de navegação. Se a encontrarmos, implementar no iniciador apenas os serviços necessários para a carregar e executar. A cada tentativa, registar o primeiro recurso ou comportamento em falta e usar esse resultado para escolher a alteração seguinte.

Para validar o resultado, guardar o comando de arranque, o registo de carregamento e uma captura antes e depois de ativar uma opção. A apresentação de uma imagem inicial, por si só, deixa por verificar a navegação. Se os ficheiros disponíveis forem insuficientes, documentar a função em falta e a evidência que levou a essa conclusão.

## Histórico da investigação de compatibilidade

As secções seguintes registam ensaios e metas anteriores com executáveis Scarlett e amostras Windows. O critério atual de conclusão está definido acima.

## Microsoft samples

[SimpleTriangle](https://learn.microsoft.com/en-us/samples/microsoft/xbox-gdk-samples/simpletriangle/) is the first console boot test. Select `Gaming.Xbox.Scarlett.x64` in the sample's Visual Studio solution. It exercises Xbox Direct3D 12 device creation, Xbox frame events and `PresentX`. These calls give us a concrete starting point for the graphics translation work.

[GameInputInterfacing](https://learn.microsoft.com/en-us/samples/microsoft/xbox-gdk-samples/gameinputinterfacing/) adds an input test on the same Xbox target. Verify that a supported controller event changes the application state shown on screen.

[NetRumble v0.3.0](https://github.com/microsoft/XBOX-Godot-NetRumble/releases/tag/v0.3.0) is a playable Microsoft sample with an Xbox Series X|S export preset. Its public ZIP contains the Godot project. The console preset names its output `scarlett_build/NetRumbleConsole.exe`. The console build requires authorized GDKX access, a console build of Godot and an authorized devkit for the official test workflow. Practice still requires access to the sample's Xbox sandbox. Follow the [sample setup documentation](https://github.com/microsoft/XBOX-Godot-NetRumble/blob/v0.3.0/tools/README.md).

## Verification

Record the sample revision, build target, executable hash and regular and delay load imports. Confirm AMD64 and a real entry point with DUMPBIN. Use the manifest or build configuration to verify Scarlett because Windows x64 executables also use AMD64. Keep the sample binaries and SDK files outside this repository.

Log the first missing import or runtime call from each authorized boot attempt. Use the public sample source to check the signature and expected behavior before implementing the next translation. Record graphics adapter and driver, reaching the first frame, controller input, the exit path and the test commands. Keep the existing Windows graphics tests green on hardware and WARP.

Signal Run now verifies the Windows side of the frame loop through `WsxStartV1`. The import inspector's `--shims` option reports matching exports, forwarded exports and missing symbols. Console startup is pending an authorized Scarlett executable and the APIs it actually calls. The Microsoft [GDK resources](https://developer.microsoft.com/en-us/games/resources/) describe access to the Xbox extensions.

## Previous GTA VI performance target

The existing Scarlett package declares `game_scarlett_master.exe`. Use a complete executable from the matching build to record the version, imports and first startup failure. A small native Scarlett sample can exercise the same class of startup work before the game is available.

The target is at least 15 presented frames per second during gameplay. Record frame intervals and the slowest one-second window, with the resolution, GPU and settings attached to the result. At 15 FPS the frame budget is 66.7 ms. A Windows build from NetRumble source supplies a separate check of local rendering and input.

## Local verification

The Windows source port of NetRumble v0.3.0 opened its own menu, entered offline Practice with bots, drew a frame and responded to simulated controller input for movement, firing and pause. It returned to the menu, quit normally and restored a saved setting on restart. The graphical test used Godot 4.6.2 in Compatibility mode at 960 × 540 on a GeForce RTX 5070 Ti Laptop GPU with NVIDIA driver 621.01. The portable executable also opened its adjacent game pack and saved successfully on window close.

The GTA VI package scan still reports the declared Scarlett executable missing. The existing graphics shim has matching exports for both observed `d3d12_xs.dll` imports in the available libraries. Six import records for `xmem.dll` have no corresponding shim. A complete Scarlett title executable is needed to observe GameOS startup requirements and measure the title's performance.

## Windows build record and console resume inputs (2026-11-21)

### Reproducible Windows result

- Source revision: `e9cbe9f23ec7a85a1b4c0f6c8cf3833ae66bc0d0` (`edupode/anyxboxseries` main; the tree tested for the v0.3.0 portable build).
- Windows sample: Signal Run, the project-owned `WsxStartV1` module. This is the playable Windows harness, not a Microsoft Scarlett build.
- Executable target: Windows x64 / AMD64. `DUMPBIN /headers bin\wsx_bootstrap.exe` reports `8664 machine (x64)` and entry point RVA `0000000140006130`. This does not establish a Scarlett target.
- Toolchain: Visual Studio 18 Community; MSVC 19.51.36263.0, toolset 14.51.36231; Windows SDK 10.0.26100.0; CMake and Ninja bundled with Visual Studio.
- Hardware adapter: NVIDIA GeForce RTX 5070 Ti Laptop GPU, DXR tier 1.2, NVIDIA driver 621.01. WARP adapter: Microsoft Basic Render Driver.
- Commands: `cmake -S native -B $env:TEMP\anyxboxseries-v0.3.0-release-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded`; `cmake --build $env:TEMP\anyxboxseries-v0.3.0-release-build`; `ctest --test-dir $env:TEMP\anyxboxseries-v0.3.0-release-build --output-on-failure`; `python -m unittest discover -s tools -p 'test_*.py' -v`.
- Results: 7/7 CTest passed (root signature, graphics probe, adapter fallback, WARP, bootstrap, Signal Run hardware, Signal Run WARP); 2/2 Python tests passed. The extracted portable package passed both Signal Run self-tests (`WSX_SAMPLE_SELF_TEST=1`, `wsx_bootstrap.exe --sample`, then `wsx_bootstrap.exe --warp --module bin\wsx_sample_game.dll`). Each completed menu/game state, movement, collision, signal collection, restart, button mapping, exit and four presented-frame pixel readbacks; module returned 0. Button mapping was exercised by the self-test; no physical controller was part of this run.
- Portable package: `AnyXboxSeries-v0.3.0-win-x64.zip`, 569,991 bytes, SHA-256 `96821A40D498395EF39A5EE7F5E7D55290D5C3B57AC2CEA92F1F8E77C8A8D7DE`. ZIP integrity check passed. Package includes `CREDITS.md` with AnyXboxSeries by edupode attribution.

### Scarlett / GTA VI not yet tested

- GDKX is not installed. The authorized SimpleTriangle and GameInputInterfacing projects and their console builds are absent. No `NetRumbleConsole.exe` or `game_scarlett_master.exe` was available in the workspace, so there is no Scarlett PE to hash, inspect with DUMPBIN, or run. No console adapter, frame, input or FPS result is claimed.
- The prior package import scan matched two `d3d12_xs.dll` imports to the prototype shim and found six `xmem.dll` import records without a candidate shim. These records still need verification against the complete executable from the same build.
- Exact inputs to resume: authorized GDKX installation and matching SDK/sample access; a SimpleTriangle build targeting `Gaming.Xbox.Scarlett.x64`; the corresponding GameInputInterfacing source/dependency package; the authorized NetRumble v0.3.0 Scarlett executable plus its matching dependencies and a testable devkit/sandbox; and a complete GTA VI build containing the manifest-declared `game_scarlett_master.exe` plus its matching package metadata and libraries. Once supplied, record executable hashes/imports, the first failing runtime call, adapter, frames, input, exit and frame intervals.

- Published release: https://github.com/edupode/anyxboxseries/releases/tag/v0.3.0

### Live portable run (2026-11-21)

- Launched the extracted `v0.3.0` package with `wsx_bootstrap.exe --sample`. The visible window title was `Signal Run | AnyXboxSeries by edupode` (process 100540).
- Pressing Enter from the menu entered the playable screen. Pressing Right moved the player and changed the signal counter from 0/3 to 1/3. This was direct keyboard input in the live window.
- A live XInputGetState probe of controller indices 0–3 returned `1167` (`ERROR_DEVICE_NOT_CONNECTED`) for all four slots. No physical XInput controller was connected. Automated tests validate the controller button mapping, but a physical controller/GameInputInterfacing run remains unverified.
- The live sample is left open for the user. Automated hardware and WARP self-tests already verified its clean exit path.

## Windows starter follow-up (2026-11-21)

### Launchers and immediate playable build

- Added `INICIAR-AGORA.cmd` at the workspace root. Double-clicking it checks for both `release\NetRumble-Windows\NetRumble.exe` and its adjacent `NetRumble.pck`, then launches the complete Windows sample from its own folder. The package includes Godot 4.6.2; no separate Godot install is needed.
- Direct launch command: `cmd.exe /d /c INICIAR-AGORA.cmd` from `C:\Users\edupo\Documents\ChatGPT\gta 6 iniciador`.
- Observed process: `NetRumble.exe`, window `NetRumble Windows (DEBUG)`, process 132844. Signal Run from the portable AnyXboxSeries package is also open as `Signal Run | AnyXboxSeries by edupode`, process 145200.
- These are runnable Windows samples. Neither window is GTA VI. The visual files in the current project are NetRumble assets; the GTA import report still says the title's `game_scarlett_master.exe` is missing.

### AnyXboxSeries v0.3.1 portable release

- Source revision/tag: `b631794e8ff0a9fc2a5026163eef56ed1b56545b` / `v0.3.1` in `edupode/anyxboxseries`.
- Executable target: Windows x64 / AMD64; `DUMPBIN /headers bin\wsx_bootstrap.exe` reports machine `8664` and entry point `0000000140006130`. This is not a Scarlett build.
- Toolchain: MSVC 19.51.36263.0 and Windows SDK 10.0.26100.0.
- Fresh regression run: 7/7 CTest passed using the Visual Studio-bundled `ctest.exe` against `%TEMP%\anyxboxseries-v0.3.0-release-build`; the local Python import-inspector suite passed 2/2. The published v0.3.1 release records 3/3 Python tests from its build.
- `Start-SignalRun.cmd` is included in the published ZIP and opens the interactive Signal Run loader sample. It was launched from a folder path containing spaces.
- Published release: https://github.com/edupode/anyxboxseries/releases/tag/v0.3.1
- Asset: `AnyXboxSeries-v0.3.1-win-x64.zip`, 570344 bytes. GitHub API digest and local SHA-256 both equal `FB355AF78ABE1E5945BCD758946FBCFD476BF2F32E4986E6EEF3AE04FBAA634A`.
- The current local `anyxboxseries` directory is a separate snapshot with no Git commits/remotes and does not contain the v0.3.1 launcher source. The public release and asset were checked independently.

### GTA VI / Scarlett remains pending

- The package report lists `game_scarlett_master.exe` as missing. The console GDKX samples, Scarlett executable and matching dependencies are also absent, so there is still no console executable to inspect or run.
- Exact resume input is unchanged: authorized GDKX installation and SimpleTriangle/GameInputInterfacing sample access; an authorized NetRumble Scarlett build and dependencies plus testable devkit/sandbox; and a complete GTA VI package containing its matching `game_scarlett_master.exe`, metadata and libraries. Until then, only the Windows samples can be launched.

## Scarlett package recheck (2026-11-21)

- Rechecked the current workspace and Downloads for `game_scarlett_master.exe`, `NetRumbleConsole.exe`, `SimpleTriangle.exe`, and `GameInputInterfacing.exe`: none were found. The GTA VI package directory exists and contains large RAGE data containers plus `gameos.xvd`, but no title executable.
- `appxmanifest.xml` declares the application executable as `game_scarlett_master.exe` and the GameOS dependency. `MicrosoftGame.Config` version `1.0.2.1` declares `TargetDeviceFamily="Scarlett"` and the same executable name. This confirms the intended target; it does not make the data containers executable.
- Confirmed no GDKX or Xbox extension SDK in the usual Microsoft GDK/Xbox install paths. The local report still shows six `xmem.dll` import records and no candidate shim.
- Microsoft's public GDK reference labels XMem as an NDA topic, and the public XMemAlloc reference requires Xbox developer-program authorization. The XMem ABI and runtime contract therefore remain unavailable here; do not invent a shim signature from import names alone.
- No RAGE archives were decrypted or modified. The supplied key does not replace the missing title executable or authorized GDKX runtime.
- Resume conditions: authorized GDKX installation/sample access; the matching full Scarlett executable and dependencies for a supported playable sample; and, for the GTA VI milestone, a complete authorized package containing `game_scarlett_master.exe`, matching metadata/libraries, plus an authorized devkit/sandbox for console validation.
- Public Microsoft references: https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/xmem/functions/xmemalloc?view=gdk-2604 ; https://learn.microsoft.com/en-us/xbox/gdk/docs/reference/system/gc-reference-system-toc?view=gdk-2604 .

## Direct3D 12 shader preview and manifest preflight (2026-11-21)

- AnyXboxSeries revision acc65429c18d6664ef61f62cd0412689dc2ef587, based on b631794e8ff0a9fc2a5026163eef56ed1b56545b. The source checkout is in AnyXboxSeries-D3D12. Review is open at https://github.com/edupode/anyxboxseries/pull/1.
- The shader fixture is original synthetic RDNA 2 code in tools/test_prepare_shader.py at that revision. The local pipeline verifies DXBC/XDAT to XBCH to FSSM, lifts a bounded scalar program to HLSL, compiles cs_6_6 with DXC and checks the D3D12 readback. The two fixture inputs exercise the outcomes 38 and 33.
- RTX 5070 Ti Laptop GPU with NVIDIA driver 621.01 and Microsoft Basic Render Driver (WARP) ran SM 6.6 and reported DXR tier 1.2. The inline DXR 1.1 probe built and cloned a BLAS, built a TLAS and verified both a hit and a miss. The extracted ZIP ran the translated shader and both ray checks with zero D3D12 debug layer errors. Signal Run also passed its extracted WARP self-test and four presented-frame pixel readbacks.
- Toolchain: MSVC 19.51.36263.0, Windows SDK 10.0.26100.0, DXC 1.9.2602.17, Visual Studio 18 CMake/Ninja. DUMPBIN /headers on the extracted wsx_shader_probe.exe reported AMD64 (8664), entry point RVA CA40. Executable SHA-256: 560d0420fd116fb7156d1db9ed52acadd10c5cf907c2e6ac830b2ab572f9e953.
- Commands: configure native with Release, Ninja and WSX_DXC_DIR pointing to the Windows SDK x64 DXC folder. Run cmake --build, ctest --test-dir <build> --output-on-failure, and python -m unittest discover -s tools -v. Results were 13/13 CTest and 13/13 Python. The extracted tests ran wsx_shader_probe --dxc-dir <sdk> --ray-query --debug with and without --warp. The shader commands are in AnyXboxSeries-D3D12/docs/SHADERS.md.
- The manifest preflight reads MicrosoftGame.Config or AppxManifest.xml, checks an available executable with DUMPBIN /headers, records its SHA-256 and highlights the first visible direct regular import gap. The positive synthetic package identified a matching d3d12_xs.dll candidate export. On the local version 1.0.2.1 package, the preflight returned 1 and selected Scarlett / game_scarlett_master.exe as missing. The support DLL scan was 16 files and 1,337 imports. Two d3d12_xs imports have candidate exports and six xmem.dll import records remain uncovered.
- Published and downloaded v0.4.0-preview.1 at https://github.com/edupode/anyxboxseries/releases/tag/v0.4.0-preview.1. The ZIP is 736,292 bytes with SHA-256 77A29DE8C50B83F0B3DB3FBC30D784A6A6E13012495EFA9B3AA804DAF95F09B4. The published file matches the verified local ZIP and includes AnyXboxSeries by edupode attribution. Leonida and ScarlettX are identified as technical references in CREDITS.md.
- Console resume input remains a complete authorized Microsoft SimpleTriangle build for Gaming.Xbox.Scarlett.x64 from the approved Xbox GDK samples and the matching dependencies, SDK version and source revision. On the first console run record DUMPBIN headers/imports and the first failed runtime call. Follow with GameInputInterfacing and an authorized playable build after presentation works. The GTA VI menu test still requires its matching authorized executable and dependencies. No Scarlett menu, controller or FPS result was measured in this run.

## Startup trace and portable preview (2026-11-21)

- Source revision: d82e31277adbe846c645a88808ef926bf1d4c7e3, parent acc65429c18d6664ef61f62cd0412689dc2ef587. The update adds a Windows child-process startup tracer, Direct3D 12 DXGI swap-chain presentation, debug-string logging, CTest coverage and complete portable-package documentation. PR #1 remains open at https://github.com/edupode/anyxboxseries/pull/1.
- Build target/toolchain: Windows x64, Visual Studio 18 2026 CMake generator, MSVC 19.51.36263.0, Windows SDK 10.0.26100.0, DXC 1.9.2602.17. Configure command: cmake -S native -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded -DWSX_DXC_DIR="C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64". Build: cmake --build build --config Release --parallel. Tests: ctest --test-dir build -C Release --output-on-failure (16/16) and python -m unittest discover -s tools (13/13).
- Adapter/results: NVIDIA GeForce RTX 5070 Ti Laptop GPU and Microsoft Basic Render Driver (WARP). The extracted ZIP ran Signal Run on both adapters, presented and read back a DXGI frame on both, and traced a WARP child process through clean exit. The extracted shader probe ran SM 6.6 and inline DXR hit/miss checks on both adapters; DXR tier 1.2 was reported and the D3D12 Debug Layer recorded 0 errors on each.
- DUMPBIN /headers on extracted wsx_graphics_probe.exe: 8664 machine (x64), entry point 0x14000BAB0. /imports confirmed direct imports d3d12_xs.dll and dxgi.dll.
- Published portable ZIP: https://github.com/edupode/anyxboxseries/releases/download/v0.4.0-preview.2/AnyXboxSeries-v0.4.0-preview.2-win-x64.zip (852,489 bytes; SHA-256 D60BD6C98D4ACDA474D63A0C205452FA059E1CDD455675AF4F0200F1139E6D07). GitHub's uploaded asset digest matched. The package includes LICENSE and CREDITS.md and contains no game archive or key.
- Current source/verification boundary: the FSSM translator/cache claim pasted by the user belongs to another person who has not pushed source. There is no code or license to inspect and that implementation is not in this release; no 100% FSSM support is claimed. The local GTA package still lacks game_scarlett_master.exe, no usable GDKX setup is installed, and XInput slots 0–3 were disconnected (1167). No Scarlett executable, physical-controller event, GTA VI menu, or console boot was tested.
- Resume console testing when an authorized GDKX installation plus a complete Scarlett SimpleTriangle build and its matching dependencies/source revision are supplied. Then record DUMPBIN imports and the first failed runtime call; continue with GameInputInterfacing and an authorized playable sample. For the separate translator, resume only when its author pushes the source or provides a reviewable archive and license. GTA VI remains blocked on a complete authorized package containing the matching title executable and dependencies.

## GameInput integration and portable preview (2026-11-21)

- Source revision: `df3f16caa2737b0d07b9a56eb44ea52697762d2f` (`v0.4.0-preview.3`) on `feature/d3d12-shader-bringup`; pushed to `edupode/anyxboxseries`. PR #1 remains open and points at this revision.
- Microsoft API-reference sample revision: `microsoft/Xbox-GDK-Samples` `b1e77b6ac10f95b07296025fda171ca2ddc42dbe`. The repository is MIT-licensed. This implementation uses the documented GameInput APIs and credits the sample; it does not copy sample source. The GameInput documentation describes `GetCurrentReading` and gamepad state access: https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/input/gameinput/interfaces/igameinput/methods/igameinput_getcurrentreading?view=gdk-2510 .
- Windows executable target: x64 / AMD64 `wsx_bootstrap.exe` loading `wsx_sample_game.dll`; `DUMPBIN /headers` on the extracted executable reports `8664 machine (x64)` and entry point RVA `00000001400062B0`. `/imports` on the sample module reports `XINPUT1_4.dll`; startup trace observes `GameInput.dll` and `GameInputRedist.dll` loaded from System32.
- Signal Run dynamically creates the Windows GameInput runtime, reads current GameInput gamepad state, maps D-pad and A/B/Y to the sample actions, and keeps XInput fallback. Runtime creation succeeded, but the API returned no current gamepad reading because no physical controller was connected. The unit test verifies mappings and no-reading HRESULTs; a real controller event remains untested.
- Manual UI observation on the extracted portable package with WARP: menu opened, keyboard Down/Up selected options, Enter started a run, Right collected a signal, and Escape closed the process. This verifies the Windows sample only; it is not a GTA VI or Scarlett boot.
- Adapters: NVIDIA GeForce RTX 5070 Ti Laptop GPU (hardware) and Microsoft Basic Render Driver (WARP); both appear in the fresh `graphics_probe` run. The existing record lists NVIDIA driver 621.01. Toolchain: Visual Studio 18 / MSVC 19.51.36263.0 and Windows SDK 10.0.26100.0.
- Exact verification commands: `cmake --build build --config Release --parallel`; `ctest --test-dir build -C Release --output-on-failure` (18/18, including hardware and WARP presentation, shader and DXR checks); `python -m unittest discover -s tools -p 'test_*.py' -v` (13/13). `WSX_SAMPLE_SELF_TEST=1 wsx_bootstrap.exe --warp --module <absolute-path-to-wsx_sample_game.dll>` passed with exit 0 and four swap-chain pixel readbacks. `wsx_startup_trace.exe --observe-ms 30000 -- <absolute-path-to-wsx_bootstrap.exe> --warp --module <absolute-path-to-wsx_sample_game.dll>` captured both GameInput DLLs and exited 0. `DUMPBIN /headers <extracted-path>\bin\wsx_bootstrap.exe` and `/imports <extracted-path>\bin\wsx_sample_game.dll` confirmed the target and XInput fallback import.
- Published portable asset: https://github.com/edupode/anyxboxseries/releases/download/v0.4.0-preview.3/AnyXboxSeries-v0.4.0-preview.3-win-x64.zip ; 856,765 bytes; SHA-256 `A55C1A3F1239C3969EC43170DDC15A72EBC383BF03996A94958335593BA75803`. GitHub reported the same SHA-256 digest. The package includes `LICENSE`, `CREDITS.md`, `CHANGELOG.md`, and `Start-SignalRun.cmd`; it contains no title executable or RAGE archive.
- No code or license was provided for the other person's claimed 100% FSSM translator/cache, so none of that implementation is copied or claimed. The existing Windows shader tests cover the repository's bounded scalar fixture only.
- Exact console resume inputs remain: an authorized GDKX installation and SDK version; matching SimpleTriangle source/revision and a `Gaming.Xbox.Scarlett.x64` build plus dependencies; GameInputInterfacing source and matching build inputs; an authorized playable Scarlett sample executable, matching libraries and manifest/package metadata; and a testable devkit/sandbox. For the GTA VI milestone, provide the complete authorized package containing `game_scarlett_master.exe`, matching metadata and libraries, and authorized console access. Then record DUMPBIN headers/imports, the first failed runtime call, adapter, controller interaction, clean exit and frame intervals. The console menu objective remains open.

## Início da investigação do menu do GTA VI (2026-11-21)

- Objetivo atual: chegar ao menu principal original do GTA VI 1.0.2.1 a partir dos ficheiros locais, sem invocar `game_scarlett_master.exe`. Só conta como êxito quando o menu real aparece e aceita uma ação.
- Diretório examinado: `C:\Users\edupo\Downloads\GTA6.Xbox.v1.0.2.1-CYBERLEEK`. `MicrosoftGame.Config` declara `TargetDeviceFamily="Scarlett"` e `game_scarlett_master.exe`. Esse executável não está presente. `Catalog000.bin` é PE AMD64 mas o DUMPBIN informa uma secção e RVA de entrada zero.
- Implementado em `AnyXboxSeries-D3D12/tools/inspect_ui_resources.py` um indexador que procura zlib streams e emite apenas offsets de stream e classes raiz, sem exportar recursos. A referência de formato foi Leonida em `aae4d99e10784eb21b4f14d08470c79613b9e7fe`; nenhum código-fonte GPL foi copiado.
- Comando executado: `python tools\inspect_ui_resources.py "C:\Users\edupo\Downloads\GTA6.Xbox.v1.0.2.1-CYBERLEEK\data.rpf" "C:\Users\edupo\Downloads\GTA6.Xbox.v1.0.2.1-CYBERLEEK\script.rpf"`.
- Resultado: 1.263 cabeçalhos RBF0 reconhecidos em `data.rpf`, incluindo 136 `UIScreen`, 65 `UIMovieView` e 61 `rage__fwuiFlowBlock`. Em `script.rpf`, 1.007 candidatos de cabeçalho `PSIN v22` com offset de primeira secção dentro do bloco. Estes candidatos não foram descodificados como scripts e não identificam qual ecrã é o primeiro do jogo.
- O scan não usou a chave fornecida, não escreveu dados dos arquivos nem alterou a build do jogo. A tabela de conteúdos RPF permanece sem nomes originais. Os arquivos ainda não revelam uma lógica executável do menu.
- Verificação Windows AnyXboxSeries: build Release concluída; CTest 19/19 passou, incluindo os caminhos do adaptador físico e WARP; Python `unittest` 13/13 passou.
- Próxima observação necessária para continuar este objetivo: uma fonte executável ou especificação autorizada da lógica do jogo que permita carregar os recursos e o fluxo de arranque original. Os dados do arquivo, por si só, ainda não identificam um caminho que produza o menu real.

## Confirmação da árvore do pacote Xbox (2026-11-21)

O utilizador confirmou que esta é a build genuína que possui; retiro a suposição anterior baseada apenas no sufixo do nome da pasta. A inspeção local só de leitura confirmou que `MicrosoftGame.Config` declara a versão 1.0.2.1 e o executável `game_scarlett_master.exe` para `TargetDeviceFamily="Scarlett"`. A procura recursiva por ficheiros `.exe` na raiz e nas pastas `data` e `pack` não encontrou nenhum. `DUMPBIN /headers Catalog000.bin` confirma AMD64/PE32+, uma secção, zero bytes de código, RVA de entrada zero e subsistema `Xbox Code Catalog`; é um catálogo, não o ponto de entrada do jogo. Não foi usada a chave nem alterado/exportado qualquer arquivo. Os ficheiros visuais e o catálogo não conseguem gerar a lógica ausente do RAGE nem o menu original.

## Investigação do Xbox2Windows (2026-11-21)

Foi revisto o repositório público `R0Y00/Xbox2Windows` no commit `b53bfb0b6489398aba7a81f3761e2d56aaa00ccf`, licença MIT. O projeto recompila funções de machine code presentes num PE Scarlett real para C++/Windows. O README confirma que um fixture criado de origem correu do entry point através de uma chamada interna e um import de DLL, mas declara ainda por verificar a execução do código CPU do título, o arranque completo do jogo e o frame rate. `tools/intake_exe.py` inspeciona um PE candidato que já existe e o `AddressOfEntryPoint`; não gera o executável ausente a partir dos arquivos RPF. Nenhum código foi copiado; a referência e a licença foram registadas em `CREDITS.md`.

## Tentativa de arranque do pacote GTA (2026-11-21)

Antes do ensaio, confirmei em `native/bootstrap.cpp` que `--build` apenas passa a pasta do pacote ao módulo Windows `wsx_boot_graphics.dll`; não resolve nem executa o EXE declarado. Comando executado a partir do ZIP extraído: `bin\wsx_bootstrap.exe --build <raiz-do-pacote-GTA>`. Resultado na NVIDIA GeForce RTX 5070 Ti Laptop GPU: criou dispositivo D3D12 Windows, leu `MicrosoftGame.Config`, desenhou o triângulo de diagnóstico do AnyXboxSeries a 160×120 (5.040 píxeis do triângulo, 14.160 de fundo) e saiu com código 0. Não é um frame do GTA.

O ensaio seguinte foi `bin\wsx_startup_trace.exe --observe-ms 30000 -- <raiz-do-pacote-GTA>\game_scarlett_master.exe`. O primeiro limite foi `CreateProcessW failed: Win32 error 2` (`ERROR_FILE_NOT_FOUND`); não houve processo de título, imports ou chamadas de runtime a analisar. A rota de arranque foi interrompida antes do D3D12 Xbox. Não repetir a mesma tentativa sem obter primeiro um executável autorizado.

Verificação complementar do pacote: o inventário contém `gameos.xvd` (348.069.888 bytes, ~331,5 MiB), mas não `.xvc` nem `game_scarlett_master.exe`. A documentação do GDK identifica `GameOs.xvd` como imagem do Game OS que o MakePkg incorpora no pacote; não é o PE do título. O ScarlettX público também declara que a desencriptação de XVC/XVD retail ainda não está implementada e que os testes usam títulos PE sintéticos; nenhum código foi copiado (o repositório consultado não expõe um ficheiro LICENSE na raiz). Fonte: [GDK deployment schema](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/packaging/deployment/atoc-deployment-schema?view=gdk-2510) · [ScarlettX README](https://github.com/Freminet/ScarlettX).

O ZIP `AnyXboxSeries-v0.4.0-preview.4-win-x64.zip` (867.883 bytes; SHA-256 `E6E430DF9F7A738F93983C840519641533E2509E8C4E2C01A271620A7B042A2D`) foi publicado na [release v0.4.0-preview.4](https://github.com/edupode/anyxboxseries/releases/tag/v0.4.0-preview.4), apontada ao commit `5498c0bbaeb59f434e55578afd9932cd8fc9047a`. O digest SHA-256 do asset devolvido pelo GitHub coincide com o ZIP local. O arquivo inclui `LICENSE` e `CREDITS.md` com atribuição edupode.

Verificação de release: CTest 19/19 passou, incluindo os testes do adaptador de hardware e WARP; Python `unittest` 13/13 passou; a sonda D3D12 confirmou NVIDIA GeForce RTX 5070 Ti Laptop GPU e o frame de diagnóstico 160×120; o self-test WARP do ZIP extraído confirmou o Signal Run e quatro leituras de frame. Não há gamepad XInput físico ligado (os quatro slots devolvem 1167) nem instalação GDKX nesta máquina.

Condição de acesso confirmada em documentação Microsoft: o GDK público não inclui as extensões necessárias para compilar `Gaming.Xbox.Scarlett.x64`. O GDKX e os downloads Xbox seguros exigem uma organização inscrita como parceira Xbox, acordo GDK válido e acesso atribuído pela Microsoft. Para retomar o boot Scarlett, é preciso obter a instalação GDKX e versão, uma build autorizada de SimpleTriangle/GameInputInterfacing e amostra jogável para Scarlett, metadados/dependências correspondentes, devkit/sandbox e um comando físico suportado. Para o marco GTA, a pasta autorizada ainda precisa incluir `game_scarlett_master.exe` e dependências correspondentes. Fontes: [GDK público e requisito GDKX](https://github.com/microsoft/GDK) · [acesso aos recursos seguros](https://learn.microsoft.com/en-us/gaming/gdk/docs/gdk-dev/development-downloads/access-resources?view=gdk-2604).
