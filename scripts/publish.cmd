@echo off
rem Publish the release build to a folder, replacing the installed program and keeping
rem the user's settings (the config folder next to the program).
rem Usage: scripts\publish.cmd [target dir] [--launch]
rem        Without a target dir a folder picker opens (double-clicking in Explorer works).
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
