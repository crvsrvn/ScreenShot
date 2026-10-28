@echo off
rem One-click release build: configure + build the win-release preset, then package
rem the portable zip and the installer into build\win-release\dist.
rem Usage: scripts\release.cmd   (double-clicking in Explorer also works)
rem (ASCII only: cmd parses this file with the OEM code page.)
setlocal
cd /d "%~dp0.."
set "ROOT=%CD%"
rem When started by double-click the window would close on exit: keep it open at the end.
set "KEEP_OPEN="
echo %cmdcmdline% | find /i "%~f0" >nul && set "KEEP_OPEN=1"

call "%~dp0vsenv.cmd" cmake --preset win-release || goto :fail
call "%~dp0vsenv.cmd" cmake --build --preset win-release || goto :fail
call "%~dp0vsenv.cmd" python -I "%ROOT%\tests\package\check_package.py" ^
    --exe "%ROOT%\build\win-release\bin\screenshot_app.exe" ^
    --windeployqt C:\Qt\6.12.0\msvc2022_64\bin\windeployqt.exe ^
    --translations "%ROOT%\build\win-release\bin\translations" ^
    --version auto ^
    --out "%ROOT%\build\win-release\dist" ^
    --iss "%ROOT%\scripts\installer.iss" || goto :fail
echo.
echo Release artifacts: %ROOT%\build\win-release\dist
echo   portable folder : dist\portable   (publish it with scripts\publish.cmd [parent dir])
echo   portable zip    : dist\ScreenShot-*-win64-portable.zip
echo   installer       : dist\ScreenShot-*-win64-setup.exe
if defined KEEP_OPEN pause
exit /b 0

:fail
echo.
echo *** Release build FAILED (see the messages above) ***
if defined KEEP_OPEN pause
exit /b 1
