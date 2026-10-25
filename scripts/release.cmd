@echo off
rem One-click release build: configure + build the win-release preset, then package
rem the portable zip and the installer into build\win-release\dist.
rem Usage: scripts\release.cmd
rem (ASCII only: cmd parses this file with the OEM code page.)
setlocal
set "ROOT=%~dp0.."
call "%~dp0vsenv.cmd" cmake --preset win-release || exit /b 1
call "%~dp0vsenv.cmd" cmake --build --preset win-release || exit /b 1
call "%~dp0vsenv.cmd" python -I "%ROOT%\tests\package\check_package.py" ^
    --exe "%ROOT%\build\win-release\bin\screenshot_app.exe" ^
    --windeployqt C:\Qt\6.12.0\msvc2022_64\bin\windeployqt.exe ^
    --translations "%ROOT%\build\win-release\bin\translations" ^
    --version auto ^
    --out "%ROOT%\build\win-release\dist" ^
    --iss "%ROOT%\scripts\installer.iss" || exit /b 1
echo.
echo Release artifacts: %ROOT%\build\win-release\dist
echo   portable folder : dist\portable   (publish it with scripts\publish.cmd [target dir])
echo   portable zip    : dist\ScreenShot-*-win64-portable.zip
echo   installer       : dist\ScreenShot-*-win64-setup.exe
