@echo off
rem Publish the release build to <parent dir>\ScreenShot, replacing the installed program and
rem keeping the user's settings (the config folder next to the program). Nothing else in the
rem parent dir is touched.
rem Usage: scripts\publish.cmd [parent dir] [--launch]
rem        Without a parent dir a folder picker opens (double-clicking in Explorer works).
rem (ASCII only: cmd parses this file with the OEM code page.)
setlocal
cd /d "%~dp0.."
set "ROOT=%CD%"
set "KEEP_OPEN="
echo %cmdcmdline% | find /i "%~f0" >nul && set "KEEP_OPEN=1"
python -I "%ROOT%\tools\package\publish.py" --source "%ROOT%\build\win-release\dist\portable" %*
set "RC=%ERRORLEVEL%"
if defined KEEP_OPEN pause
exit /b %RC%
