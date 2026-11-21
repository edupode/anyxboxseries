@rem Copyright (c) 2026 edupode
@rem SPDX-License-Identifier: CPAL-1.0
@echo off
setlocal
set "WSX_BOOTSTRAP=%~dp0bin\wsx_bootstrap.exe"
if not exist "%WSX_BOOTSTRAP%" (
    echo AnyXboxSeries bootstrap not found: "%WSX_BOOTSTRAP%"
    pause
    exit /b 1
)
start "" "%WSX_BOOTSTRAP%" --sample
