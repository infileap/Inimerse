; Inimerse / Infiverse installer
; Build from the repository root with Inno Setup 6 ISCC.exe.
[Setup]
AppId={{A8A1D2C7-4A1B-4E80-9E13-8F2B3A5D6C11}
AppName=Infiverse
AppVersion=0.2.0
AppPublisher=Infiverse
DefaultDirName={autopf}\Infiverse
DefaultGroupName=Infiverse
UninstallDisplayIcon={app}\app.exe
OutputDir=D:\Infiverse_release
OutputBaseFilename=InfiverseSetup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
SetupIconFile={#SourcePath}\icon.ico
ArchitecturesInstallIn64BitMode=x64

[Files]
Source: "{#SourcePath}\Infiverse_standard\src-tauri\target\release\app.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourcePath}\inimerse.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourcePath}\Infiverse_standard\plugins\*"; DestDir: "{app}\plugins"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#SourcePath}\Infiverse_standard\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourcePath}\docs\OAUTH.md"; DestDir: "{app}\docs"; Flags: ignoreversion

[Dirs]
Name: "{app}\userdata"
Name: "{app}\projects"

[Icons]
Name: "{group}\Infiverse"; Filename: "{app}\app.exe"
Name: "{commondesktop}\Infiverse"; Filename: "{app}\app.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Run]
Filename: "{app}\app.exe"; Description: "Launch Infiverse"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\plugins"
