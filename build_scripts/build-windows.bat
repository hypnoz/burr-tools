@echo off
rem Launcher for lib\build-windows.ps1 so cmd.exe / Explorer do not need a
rem loosened execution policy.
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0lib\build-windows.ps1" %*
exit /b %ERRORLEVEL%
