#define MyAppName "ESPCube Companion"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "ESPCube"
#define MyAppExeName "ESPCube Companion.exe"

[Setup]
AppId={{D4A6A72E-47CE-4E39-98CB-478210000001}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\ESPCube Companion
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=output
OutputBaseFilename=ESPCube-Companion-v1.0.0-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=ESPCube Companion
SetupLogging=yes

[Files]
Source: "release\ESPCube Companion.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "release\models\ggml-tiny.en.bin"; DestDir: "{app}\models"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\ESPCube Companion"; Filename: "{app}\{#MyAppExeName}"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "ESPCube Companion"; ValueData: """{app}\{#MyAppExeName}"" --background"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch ESPCube Companion"; Flags: nowait postinstall skipifsilent
