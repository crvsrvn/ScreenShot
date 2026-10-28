; ScreenShot 安装包（NFR-12）。由 tests/package/check_package.py 调用：
;   ISCC /DAppVersion=0.1.0 /DSourceDir=<windeployqt 输出目录> /DOutDir=<dist> installer.iss
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\build\win-release\dist\portable"
#endif
#ifndef OutDir
  #define OutDir "..\build\win-release\dist"
#endif

[Setup]
AppId={{8C4B2C6E-5B5B-4C0F-9D2A-ScreenShotApp}}
AppName=ScreenShot
AppVersion={#AppVersion}
AppPublisher=ScreenShot
DefaultDirName={autopf}\ScreenShot
DefaultGroupName=ScreenShot
OutputDir={#OutDir}
OutputBaseFilename=ScreenShot-{#AppVersion}-win64-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayIcon={app}\screenshot_app.exe
SetupIconFile=..\src\app\icons\app.ico

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\ScreenShot"; Filename: "{app}\screenshot_app.exe"
Name: "{group}\Uninstall ScreenShot"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\screenshot_app.exe"; Description: "Launch ScreenShot"; Flags: nowait postinstall skipifsilent
