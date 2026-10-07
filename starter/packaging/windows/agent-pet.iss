; Agent Pet setup program, compiled by scripts/package_windows.py with Inno Setup 6:
;   iscc /DAppVersion=X.Y.Z /DSourceDir=<staged files> /DOutputDir=<dir> /DOutputBaseFilename=<name> agent-pet.iss
; A per-user installation: no administrator rights, like the Linux package's default.

#ifndef AppVersion
  #error Define AppVersion, SourceDir, OutputDir and OutputBaseFilename
#endif

[Setup]
AppId={{8441EED6-7EE4-4267-AADE-8840188C4202}
AppName=Agent Pet
AppVersion={#AppVersion}
AppVerName=Agent Pet {#AppVersion}
AppPublisher=Agent Pet
AppPublisherURL=https://github.com/WindyWin/vpet-agent-pet
AppSupportURL=https://github.com/WindyWin/vpet-agent-pet/issues
AppUpdatesURL=https://github.com/WindyWin/vpet-agent-pet/releases
VersionInfoVersion={#AppVersion}
; No spaces: Codex starts hooks through cmd.exe, which cannot run a quoted program.
DefaultDirName={autopf}\AgentPet
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 1809, the oldest release Qt 6.5 supports.
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
SetupIconFile=agent-pet.ico
UninstallDisplayIcon={app}\agent-pet.exe
UninstallDisplayName=Agent Pet
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; A running pet holds its files open: close it to upgrade or remove it.
CloseApplications=force
RestartApplications=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "claude"; Description: "Connect Claude Code (adds Agent Pet's hooks; Claude Code 2.1.139 or newer)"; GroupDescription: "Agents (restart them afterwards):"; Flags: unchecked
Name: "codex"; Description: "Connect Codex (adds Agent Pet's hooks; trust them in Codex /hooks)"; GroupDescription: "Agents (restart them afterwards):"; Flags: unchecked
Name: "login"; Description: "Start Agent Pet when I sign in"; GroupDescription: "Startup:"; Flags: unchecked
Name: "autostart"; Description: "Start the pet when an agent session starts"; GroupDescription: "Startup:"; Flags: unchecked

[Files]
; Artwork packs are named by content. Packs an upgrade no longer lists stay until uninstall (the pet loads
; only those artwork.rcc names), so a failed or cancelled upgrade never leaves the old version without artwork.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Agent Pet"; Filename: "{app}\agent-pet.exe"
Name: "{autodesktop}\Agent Pet"; Filename: "{app}\agent-pet.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\agent-pet.exe"; Description: "{cm:LaunchProgram,Agent Pet}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Only Agent Pet's own hook entries are removed; other hooks are left as they were. The running pet and
; the login value are handled in [Code], limited to this installation.
Filename: "{app}\agent-pet-cli.exe"; Parameters: "integration disable --provider claude"; Flags: runhidden; RunOnceId: "DisableClaude"
Filename: "{app}\agent-pet-cli.exe"; Parameters: "integration disable --provider codex"; Flags: runhidden; RunOnceId: "DisableCodex"

[Code]
// Runs one setup command for a selected task and reports a failure instead of hiding it.
procedure Configure(const Task, Parameters, Failure: String);
var
  ResultCode: Integer;
begin
  if not WizardIsTaskSelected(Task) then Exit;
  if not Exec(ExpandConstant('{app}\agent-pet-cli.exe'), Parameters, '', SW_HIDE, ewWaitUntilTerminated, ResultCode)
     or (ResultCode <> 0) then
    SuppressibleMsgBox(Failure + #13#10#13#10 + 'You can retry from the pet: right-click it > Settings > Startup and agents.',
                       mbError, MB_OK, IDOK);
end;

const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';

// Stops pets started from this installation only; a portable copy elsewhere keeps running.
procedure StopInstalledPet();
var
  Folder: String;
  ResultCode: Integer;
begin
  Folder := ExpandConstant('{app}') + '\';
  StringChangeEx(Folder, '''', '''''', True);
  Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
       '-NoProfile -NonInteractive -Command "Get-Process -Name agent-pet -ErrorAction SilentlyContinue | ' +
       'Where-Object { $_.Path -and $_.Path.StartsWith(''' + Folder + ''', [StringComparison]::OrdinalIgnoreCase) } | ' +
       'Stop-Process -Force"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

// Removes the login value only when it starts this installation, not another copy's registration.
procedure RemoveInstalledLogin();
var
  Value: String;
begin
  if RegQueryStringValue(HKCU, RunKey, 'Agent Pet', Value)
     and (CompareText(Value, '"' + ExpandConstant('{app}\agent-pet.exe') + '"') = 0) then
    RegDeleteValue(HKCU, RunKey, 'Agent Pet');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then begin
    StopInstalledPet();
    RemoveInstalledLogin();
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep <> ssPostInstall then Exit;
  Configure('claude', 'integration enable --provider claude', 'Agent Pet could not connect Claude Code.');
  Configure('codex', 'integration enable --provider codex', 'Agent Pet could not connect Codex.');
  Configure('login', 'autostart login enable', 'Agent Pet could not register itself to start at sign-in.');
  Configure('autostart', 'autostart enable', 'Agent Pet could not turn on starting with agent sessions.');
end;
