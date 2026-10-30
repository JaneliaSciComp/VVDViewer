#define MyAppName "VVDViewer"

#define MyAppExeName "VVDViewer.exe"

[Setup]

AppName={#MyAppName}
AppId = {#MyAppName}AppId
AppMutex = {#MyAppName}MutexName
AppVerName={#MyAppName} 1.00
OutputBaseFilename={#MyAppName}_setup
DefaultDirName={pf}\{#MyAppName}
DefaultGroupName={#MyAppName}
UsePreviousAppDir=yes
AppendDefaultDirName=yes 
OutputDir=.\
Compression=lzma2
SolidCompression=yes
; "ArchitecturesAllowed=x64" specifies that Setup cannot run on
; anything but x64.
ArchitecturesAllowed=x64
; "ArchitecturesInstallIn64BitMode=x64" requests that the install be
; done in "64-bit mode" on x64, meaning it should use the native
; 64-bit Program Files directory and the 64-bit view of the registry.
ArchitecturesInstallIn64BitMode=x64

ShowTasksTreeLines=yes

ChangesAssociations=yes
DisableWelcomePage=no

[Files]
Source: "build\bin\Release\VVDViewer.exe"; DestDir: "{app}"; Flags: ignoreversion touch
Source: "VVD_Viewer\Settings\*"; DestDir: "{app}"; Flags: ignoreversion touch; Permissions: users-modify
Source: "VVD_Viewer\Fonts\*"; DestDir: "{app}\Fonts"; Flags: ignoreversion touch
Source: "VVD_Viewer\Scripts\*"; DestDir: "{app}\Scripts"; Flags: ignoreversion touch
Source: "VVD_Viewer\CL_code\*"; DestDir: "{app}\CL_code"; Flags: ignoreversion touch
Source: "VVD_Viewer\ColorDepthMIP\*"; DestDir: "{app}\ColorDepthMIP"; Flags: ignoreversion touch

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: desktopicon; Description: "Create a &desktop icon"
Name: vvdAssociation; Description: "Associate ""vvd"" files"; GroupDescription: File extensions:

[Registry]
Root: HKCR; Subkey: ".vvd";                             ValueData: "{#MyAppName}";          Flags: uninsdeletevalue; ValueType: string;  ValueName: "" ; Tasks: vvdAssociation
Root: HKCR; Subkey: "{#MyAppName}";                     ValueData: "Program {#MyAppName}";  Flags: uninsdeletekey;   ValueType: string;  ValueName: "" ; Tasks: vvdAssociationRoot: HKCR; Subkey: "{#MyAppName}\shell\open\command";  ValueData: """{app}\{#MyAppExeName}"" ""%1""";               ValueType: string;  ValueName: "" ; Tasks: vvdAssociation