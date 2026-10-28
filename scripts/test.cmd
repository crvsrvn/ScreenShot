@echo off
rem Run the whole test suite with one command and write reports (TST-10).
rem Usage: scripts\test.cmd [preset] [extra ctest args]   (default preset: win-debug)
setlocal
rem shift also shifts %0, so keep the script dir first
set "HERE=%~dp0"
rem CMakePresets.json is in src
cd /d "%HERE%..\src"
set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=win-debug"
shift
call "%HERE%vsenv.cmd" ctest --preset %PRESET% --output-junit "%HERE%..\build\%PRESET%\test-results\ctest-junit.xml" %1 %2 %3 %4 %5 %6
