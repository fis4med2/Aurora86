#ifndef SourceDir
  #error SourceDir must point to the deployed application folder.
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef AppVersion
  #define AppVersion "1.5.0"
#endif

[Setup]
AppId={{37A66C52-67FD-4652-8ED3-83814522A198}
AppName=AURORA/86
AppVersion={#AppVersion}
AppPublisher=AURORA/86 contributors
AppPublisherURL=https://github.com/fis4med2/Aurora86
AppSupportURL=https://github.com/fis4med2/Aurora86/issues
DefaultDirName={localappdata}\Programs\AURORA86
DefaultGroupName=AURORA86
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=AURORA86-Setup-x64
SetupIconFile={#SourceDir}\aurora86.ico
UninstallDisplayIcon={app}\aurora86.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile={#SourceDir}\LICENSE
ChangesAssociations=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\AURORA86"; Filename: "{app}\aurora86.exe"; IconFilename: "{app}\aurora86.ico"
Name: "{autodesktop}\AURORA86"; Filename: "{app}\aurora86.exe"; IconFilename: "{app}\aurora86.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\aurora86.exe"; Description: "{cm:LaunchProgram,AURORA86}"; Flags: nowait postinstall skipifsilent

; The simulator's XDG/AppData virtual disk lives outside {app}.
; No UninstallDelete rule is used: removing the program preserves user files.
