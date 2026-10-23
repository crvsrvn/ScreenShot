@echo off
rem Configure + build. Usage: scripts\build.cmd [preset]  (default: win-debug)
setlocal
set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=win-debug"
call "%~dp0vsenv.cmd" cmake --preset %PRESET% || exit /b 1
call "%~dp0vsenv.cmd" cmake --build --preset %PRESET% || exit /b 1
