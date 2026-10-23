@echo off
rem Enter the MSVC x64 environment, then run the given command. Usage: scripts\vsenv.cmd <command...>
rem (ASCII only: cmd parses this file with the OEM code page.)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSROOT="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if "%VSROOT%"=="" (
    echo MSVC installation not found 1>&2
    exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
set "PATH=C:\Qt\6.12.0\msvc2022_64\bin;%PATH%"
%*
