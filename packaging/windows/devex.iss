; The Windows installer of the engine: what `cmake --install` gives, for one user, without administrator rights. It adds
; a shortcut, opens .dvxproj projects with the editor, and uninstalls. scripts/prepare_release.ps1 calls it:
;
;   iscc /DAppVersion=0.9.0 /DSource=<installed folder> /DOutputFolder=<folder> packaging\windows\devex.iss

#ifndef AppVersion
  #error Give the version: /DAppVersion=0.9.0
#endif
#ifndef Source
  #error Give the installed engine: /DSource=<folder>
#endif
#ifndef OutputFolder
  #define OutputFolder "."
#endif

[Setup]
; Never to change: Windows knows an installed engine by it, and an update replaces it.
AppId={{6F0B8C52-3A1E-4B7D-9E26-D1C4A7F05B13}
AppName=Devex Engine
AppVersion={#AppVersion}
AppVerName=Devex Engine {#AppVersion}
AppPublisher=Devex Engine contributors
AppPublisherURL=https://github.com/D4EMONDEV/Devex-Engine
AppSupportURL=https://github.com/D4EMONDEV/Devex-Engine/issues
DefaultDirName={autopf}\Devex Engine
DefaultGroupName=Devex Engine
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#Source}\LICENSE
SetupIconFile={#SourcePath}..\..\engine\resources\icons\devex.ico
UninstallDisplayIcon={app}\bin\devex-editor.exe
OutputDir={#OutputFolder}
OutputBaseFilename=devex-{#AppVersion}-windows-x64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
; The running editor holds its files.
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associate"; Description: "{cm:AssocFileExtension,Devex Engine,.dvxproj}"

[InstallDelete]
; An update leaves nothing of the version it replaces, whose libraries would still be found.
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\cmake"
Type: filesandordirs; Name: "{app}\include"
Type: filesandordirs; Name: "{app}\lib"

[Files]
Source: "{#Source}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Devex Engine"; Filename: "{app}\bin\devex-editor.exe"
Name: "{autodesktop}\Devex Engine"; Filename: "{app}\bin\devex-editor.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.dvxproj\OpenWithProgids"; ValueType: string; ValueName: "DevexEngine.Project"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.dvxproj"; ValueType: string; ValueName: ""; ValueData: "DevexEngine.Project"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\DevexEngine.Project"; ValueType: string; ValueName: ""; ValueData: "Devex Project"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\DevexEngine.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\bin\devex-editor.exe,0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\DevexEngine.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\devex-editor.exe"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\bin\devex-editor.exe"; Description: "{cm:LaunchProgram,Devex Engine}"; Flags: nowait postinstall skipifsilent
