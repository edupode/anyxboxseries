@echo off
setlocal

rem Start the complete Windows game currently available in this folder.
rem NetRumble includes the Godot runtime and requires NetRumble.pck beside the executable.
set "GAME_DIR=%~dp0release\NetRumble-Windows"
set "GAME_EXE=%GAME_DIR%\NetRumble.exe"
set "GAME_PCK=%GAME_DIR%\NetRumble.pck"

if not exist "%GAME_EXE%" (
  echo Missing: "%GAME_EXE%"
  pause
  exit /b 1
)
if not exist "%GAME_PCK%" (
  echo Missing: "%GAME_PCK%"
  echo Keep the resource package beside the executable.
  pause
  exit /b 1
)

echo Starting the available NetRumble Windows game.
echo This is the playable sample in this folder, not GTA VI.
start "NetRumble" /D "%GAME_DIR%" "%GAME_EXE%"
if errorlevel 1 (
  echo Windows could not start NetRumble.
  pause
  exit /b 1
)
exit /b 0
