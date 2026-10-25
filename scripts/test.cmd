@echo off
rem Run the whole test suite with one command and write reports (TST-10).
rem Usage: scripts\test.cmd [preset] [extra ctest args]   (default preset: win-debug)
setlocal
cd /d "%~dp0.."
set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=win-debug"
shift
call "%~dp0vsenv.cmd" ctest --preset %PRESET% --output-junit "%~dp0..\build\%PRESET%\test-results\ctest-junit.xml" %1 %2 %3 %4 %5 %6
