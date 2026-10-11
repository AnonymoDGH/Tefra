; Tefra installer (Inno Setup 6). Built by .github/workflows/release.yml from the staged folder `dist\Tefra`.
; Per-user install (no administrator rights), Start menu and desktop shortcuts for Tefra Studio.
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\Tefra"
#endif

[Setup]
AppId={{6E1C2B0A-7F3D-4C2B-9C8E-5A1F0B7D2E41}
AppName=Tefra
AppVersion={#AppVersion}
AppVerName=Tefra {#AppVersion}
AppPublisher=Tefra contributors
AppPublisherURL=https://github.com/AnonymoDGH/Tefra
DefaultDirName={localappdata}\Programs\Tefra
DefaultGroupName=Tefra
PrivilegesRequired=lowest
OutputBaseFilename=Tefra-{#AppVersion}-Setup
OutputDir=..\dist
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
LicenseFile=..\LICENSE
UninstallDisplayIcon={app}\tefra_studio.exe
DisableProgramGroupPage=yes

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Tefra Studio"; Filename: "{app}\tefra_studio.exe"; WorkingDir: "{app}"
Name: "{group}\Tefra Benchmark (enviar resultados)"; Filename: "{app}\bench-windows.bat"; WorkingDir: "{app}"
Name: "{group}\Desinstalar Tefra"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Tefra Studio"; Filename: "{app}\tefra_studio.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\tefra_studio.exe"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,Tefra Studio}"; Flags: nowait postinstall skipifsilent

[Code]
function DotNet8Installed(): Boolean;
var
  Dirs: TArrayOfString;
begin
  Result := DirExists(ExpandConstant('{commonpf64}\dotnet\shared\Microsoft.NETCore.App')) and
            (FileExists(ExpandConstant('{commonpf64}\dotnet\host\fxr\8.0.0\hostfxr.dll')) or
             DirExists(ExpandConstant('{commonpf64}\dotnet\host\fxr')));
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
begin
  if (CurStep = ssDone) and not DotNet8Installed() then
    if MsgBox('Los scripts C# de Tefra necesitan el runtime de .NET 8 (o el SDK).' + #13#10 +
              'Sin él, Studio funciona, pero los scripts no se ejecutan.' + #13#10#13#10 +
              '¿Abrir la página de descarga de .NET 8?', mbConfirmation, MB_YESNO) = IDYES then
      ShellExec('open', 'https://dotnet.microsoft.com/download/dotnet/8.0', '', '', SW_SHOWNORMAL, ewNoWait, Code);
end;
