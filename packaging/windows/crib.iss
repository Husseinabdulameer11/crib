; Inno Setup script for the Crib Windows installer.
;
; Built by the release workflow; to build it yourself (Inno Setup 6.3+):
;
;   make VERSION=0.1.0 LDFLAGS=-static            (in MSYS2 UCRT64)
;   packaging/package.sh windows-x86_64
;   iscc /DAppVersion=0.1.0 packaging\windows\crib.iss
;
; Output: dist\crib-windows-setup.exe
;
; The installer puts crib.exe in <install dir>\bin and adds that folder to
; PATH, so `crib` works in any NEW terminal. By default it installs for the
; current user only (no admin prompt, PATH is the user's PATH); the first
; page offers "install for all users" instead (needs admin, system PATH).
; Uninstalling removes the folder from PATH again.

#ifndef AppVersion
  #define AppVersion "dev"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\crib-windows-x86_64"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif
#ifndef Repo
  #define Repo "Husseinabdulameer11/crib"
#endif
#define AppPublisher "Crib contributors"

[Setup]
; AppId identifies Crib to Windows across versions: never change it, or an
; update installs side by side with the old version instead of replacing it.
AppId={{468C5290-CA5B-4F5B-A7B3-F03B6181BD46}
AppName=Crib
AppVersion={#AppVersion}
AppVerName=Crib {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL=https://github.com/{#Repo}
AppSupportURL=https://github.com/{#Repo}/issues
AppUpdatesURL=https://github.com/{#Repo}/releases
DefaultDirName={autopf}\Crib
DefaultGroupName=Crib
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesEnvironment=yes
OutputDir={#OutputDir}
OutputBaseFilename=crib-windows-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\bin\crib.exe
UninstallDisplayName=Crib {#AppVersion}
VersionInfoProductTextVersion={#AppVersion}

[Tasks]
Name: "addtopath"; Description: "Add crib to PATH, so the crib command works in any terminal (recommended)"

[Files]
Source: "{#SourceDir}\crib.exe";   DestDir: "{app}\bin"; Flags: ignoreversion
Source: "{#SourceDir}\README.md";  DestDir: "{app}";     Flags: ignoreversion
Source: "{#SourceDir}\LICENSE";    DestDir: "{app}";     Flags: ignoreversion
Source: "{#SourceDir}\examples\*"; DestDir: "{app}\examples"; Flags: ignoreversion recursesubdirs

[Icons]
; A terminal with crib ready, starting in Documents (writable, unlike the
; install folder, so `crib --html page.html ...` can save there).
Name: "{group}\Crib command prompt"; Filename: "{cmd}"; \
    Parameters: "/k set ""PATH={app}\bin;%PATH%"" && crib --help"; WorkingDir: "{userdocs}"
Name: "{group}\Crib examples"; Filename: "{app}\examples"
Name: "{group}\Uninstall Crib"; Filename: "{uninstallexe}"

[Messages]
FinishedLabel=Crib is installed.%n%nOpen a NEW terminal (PowerShell or cmd; ones that were already open do not see the new PATH) and run:%n%n    crib --help%n%nThe Start menu has a Crib command prompt and the examples folder.

[Code]
const
  SystemEnvKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';
  UserEnvKey   = 'Environment';

{ All-users install -> system PATH; current-user install -> user PATH. }
function EnvRoot: Integer;
begin
  if IsAdminInstallMode then Result := HKEY_LOCAL_MACHINE
  else Result := HKEY_CURRENT_USER;
end;

function EnvKey: String;
begin
  if IsAdminInstallMode then Result := SystemEnvKey
  else Result := UserEnvKey;
end;

{ Position of Dir in ';' + Paths + ';' (case-insensitive), 0 if absent.
  A match at P means Dir starts at index P of Paths itself. }
function FindInPath(Paths, Dir: String): Integer;
begin
  Result := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Paths) + ';');
end;

procedure AddToPath(Dir: String);
var
  Paths: String;
begin
  if not RegQueryStringValue(EnvRoot, EnvKey, 'Path', Paths) then
    Paths := '';
  if FindInPath(Paths, Dir) > 0 then
    exit;
  if (Paths <> '') and (Copy(Paths, Length(Paths), 1) <> ';') then
    Paths := Paths + ';';
  Paths := Paths + Dir;
  if not RegWriteExpandStringValue(EnvRoot, EnvKey, 'Path', Paths) then
    SuppressibleMsgBox('Could not add ' + Dir + ' to PATH. You can add it by ' +
                       'hand in "Edit environment variables".', mbError, MB_OK, IDOK);
end;

procedure RemoveFromPath(Dir: String);
var
  Paths: String;
  P: Integer;
begin
  if not RegQueryStringValue(EnvRoot, EnvKey, 'Path', Paths) then
    exit;
  P := FindInPath(Paths, Dir);
  if P = 0 then
    exit;
  if P > 1 then
    Delete(Paths, P - 1, Length(Dir) + 1)   { the ';' before it, and Dir }
  else
    Delete(Paths, 1, Length(Dir) + 1);      { Dir, and the ';' after it }
  RegWriteExpandStringValue(EnvRoot, EnvKey, 'Path', Paths);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addtopath') then
    AddToPath(ExpandConstant('{app}\bin'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath(ExpandConstant('{app}\bin'));
end;
