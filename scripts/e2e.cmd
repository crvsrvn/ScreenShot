@echo off
rem Run the end-to-end suite on the real desktop. Run this from the console session you are
rem sitting at (double-click works); a disconnected or locked session has no usable desktop and
rem the input scenarios are skipped there.
rem Usage: scripts\e2e.cmd [preset]   (default: win-release-test)
rem (ASCII only: cmd parses this file with the OEM code page.)
setlocal
rem CMakePresets.json is in src
cd /d "%~dp0..\src"
set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=win-release-test"
set "KEEP_OPEN="
echo %cmdcmdline% | find /i "%~f0" >nul && set "KEEP_OPEN=1"
echo Session: %SESSIONNAME%
call "%~dp0vsenv.cmd" ctest --preset %PRESET% -L e2e --output-on-failure
set "RC=%ERRORLEVEL%"
echo.
echo Report: build\%PRESET%\test-results\e2e_m0.json
if defined KEEP_OPEN pause
exit /b %RC%
