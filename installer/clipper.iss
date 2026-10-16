; Clipper installer (Inno Setup 6). Built by make_installer.cmd from build-release\stage.
; Per-user install: no administrator rights needed.

#define AppName "Clipper"
#define AppVersion "1.0.0"
#ifndef StageDir
  #define StageDir "..\build-release\stage"
#endif

[Setup]
AppId={{6B4D2C1E-8F0A-4B7E-9C3D-2A5E7F1B0C44}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppName}
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=Clipper-Setup-{#AppVersion}
SetupIconFile=..\src\app\clipper.ico
UninstallDisplayIcon={app}\clipper.exe
UninstallDisplayName={#AppName}
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
CloseApplications=force
RestartApplications=no
ShowLanguageDialog=no

[Languages]
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "{cm:AutoStart}"; GroupDescription: "{cm:Other}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\clipper.exe"; Parameters: "--show"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\clipper.exe"; Parameters: "--show"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Clipper"; \
  ValueData: """{app}\clipper.exe"""; Tasks: autostart; Flags: uninsdeletevalue
; Also remove the value if the user enabled autostart from the app's settings.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "Clipper"; Flags: dontcreatekey uninsdeletevalue

[Run]
Filename: "{app}\clipper.exe"; Parameters: "--show"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{cmd}"; Parameters: "/C taskkill /IM clipper.exe /F"; Flags: runhidden; RunOnceId: "StopClipper"

[UninstallDelete]
; Caches only: logs, thumbnails, replay buffer. Settings and saved clips are kept.
Type: filesandordirs; Name: "{localappdata}\Clipper"

[CustomMessages]
it.AutoStart=Avvia Clipper all'accensione del PC
en.AutoStart=Start Clipper when Windows starts
it.Other=Altro:
en.Other=Other:
