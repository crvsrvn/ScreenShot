@echo off
rem Publish the release build to a folder, replacing the installed program and keeping
rem the user's settings (the config folder next to the program).
rem Usage: scripts\publish.cmd [target dir] [--launch]
rem        Without a target dir a folder picker opens.
rem (ASCII only: cmd parses this file with the OEM code page.)
setlocal
set "ROOT=%~dp0.."
python -I "%ROOT%\tools\package\publish.py" --source "%ROOT%\build\win-release\dist\portable" %*
