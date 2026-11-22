@echo off
setlocal
python -B "%~dp0..\..\tools\rpf_explorer.py" %*
if errorlevel 1 pause
