; SPDX-License-Identifier: BSD-3-Clause
;
; pspdisp.iss - Installer of PSPdisp for Windows (Inno Setup 6.3 or later)
;
; Copyright © 2026 Arthur Neto
;
; Compiled by `make` (see inno.mk), from the folder it stages (StageDir), with:
; - bin: the programs, with the DLLs of MSYS2 they need (GTK, GLib, ...), as GTK finds its data from the folder above (lib and share)
; - lib, share: the data of GTK (the GSettings schemas, the icons, and the SVG loader of gdk-pixbuf)
; - drivers: the driver packages (the virtual display, the virtual gamepad and the WinUSB driver of the PSP), and pspdisp-driver.exe that installs them (see driver.c)
; - pspdisp-winsign.cer: the self-signed certificate of the driver packages, trusted by the installer, so Windows installs them
; - LICENSE, com.pspdisp.ui.ico
;
; The installer also removes PSPdisp 0.6 (the old desktop app, installed by NSIS) and, if asked, its settings,
; starts the daemon at sign-in, and allows the WLAN mode through the Windows Firewall.

#ifndef AppVersion
  #error "AppVersion is not set, compile it with make"
#endif
#ifndef FileVersion
  #error "FileVersion is not set, compile it with make"
#endif
#ifndef StageDir
  #error "StageDir is not set, compile it with make"
#endif
#if VER < EncodeVer(6, 3, 0)
  #error "Inno Setup 6.3 or later is needed (ExecAndCaptureOutput)"
#endif

; The first TCP port of the WLAN mode, and the last one with the biggest port offset set on the PSP (see wlan/wlan.c and settings/settings.c)
#define WlanPorts "17584-17593"

[Setup]
; Never change it: Windows knows an installed PSPdisp by it, to update or remove it
AppId={{1AC64F4A-3E68-4BDC-BF3A-87D9925A7CFB}
AppName=PSPdisp
AppVersion={#AppVersion}
AppVerName=PSPdisp {#AppVersion}
AppPublisher=PSPdisp
AppPublisherURL=https://github.com/net-0/PSPdisp
AppSupportURL=https://github.com/net-0/PSPdisp/issues
VersionInfoVersion={#FileVersion}
DefaultDirName={autopf}\PSPdisp
DefaultGroupName=PSPdisp
DisableProgramGroupPage=yes
LicenseFile={#StageDir}\LICENSE
SetupIconFile={#StageDir}\com.pspdisp.ui.ico
UninstallDisplayIcon={app}\bin\pspdisp-ui.exe
UninstallDisplayName=PSPdisp
OutputBaseFilename=PSPdisp-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; The drivers and the certificate need an administrator, and the drivers are only built for x64
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 1709, for IddCx 1.2 (see the INF of the virtual display)
MinVersion=10.0.16299
; The programs are stopped by the installer itself (see PrepareToInstall), the daemon has no window to be closed by
CloseApplications=no
; The only area of the user is the folder of PSPdisp 0.6 (the "oldsettings" task), the one of the user who installs it, as the old installer put it there
UsedUserAreasWarning=no

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "main"; Description: "PSPdisp (the desktop app and the daemon)"; Types: full custom; Flags: fixed
Name: "display"; Description: "Virtual display (IddCx driver), the display the PSP shows"; Types: full
Name: "gamepad"; Description: "Virtual gamepad (a DualShock 4), driven by the PSP buttons"; Types: full
Name: "winusb"; Description: "WinUSB driver of the PSP, for the ""Type B"" USB mode of the PSP app"; Types: full

[Tasks]
Name: "autostart"; Description: "Start PSPdisp when I sign in, so it waits for the PSP"
Name: "firewall"; Description: "Allow the PSP through the Windows Firewall, for the WLAN mode (TCP {#WlanPorts})"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "oldsettings"; Description: "Delete the settings of PSPdisp 0.6 (its control files in {userappdata}\PSPdisp)"; Check: OldSettingsExist

[Files]
Source: "{#StageDir}\bin\*"; DestDir: "{app}\bin"; Flags: ignoreversion recursesubdirs
Source: "{#StageDir}\lib\*"; DestDir: "{app}\lib"; Flags: ignoreversion recursesubdirs
Source: "{#StageDir}\share\*"; DestDir: "{app}\share"; Flags: ignoreversion recursesubdirs
Source: "{#StageDir}\drivers\*"; DestDir: "{app}\drivers"; Flags: ignoreversion recursesubdirs
Source: "{#StageDir}\pspdisp-winsign.cer"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
; The files of the previous version, so none is left from it (e.g. a DLL that GTK doesn't need anymore)
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\lib"
Type: filesandordirs; Name: "{app}\share"
Type: filesandordirs; Name: "{app}\drivers"
Type: filesandordirs; Name: "{userappdata}\PSPdisp"; Tasks: oldsettings

[Icons]
Name: "{autoprograms}\PSPdisp"; Filename: "{app}\bin\pspdisp-ui.exe"; Comment: "Use a PSP as a display of the computer, with its sound and its controls"
Name: "{autodesktop}\PSPdisp"; Filename: "{app}\bin\pspdisp-ui.exe"; Tasks: desktopicon

[Registry]
; The daemon, for every user, as they each run their own (it runs as the user, not as an administrator)
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "PSPdisp"; ValueData: """{app}\bin\pspdisp-daemon.exe"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
; As the user who started the installer, not as the administrator: the daemon refuses to run elevated, and the app starts it
Filename: "{app}\bin\pspdisp-ui.exe"; Description: "{cm:LaunchProgram,PSPdisp}"; Flags: postinstall nowait skipifsilent runasoriginaluser

[UninstallDelete]
; Made by the programs (e.g. the cache of GLib), the settings of the users are kept (in their AppData\Local\pspdisp)
Type: filesandordirs; Name: "{app}"

[Code]
var
  Restart: Boolean; // Windows has to restart to finish installing or removing a driver
  DriverErrors: String; // What failed while installing the drivers, shown at the end

// Run a program hidden and wait for it, giving its exit code, and its output in Output
function RunHidden(const FileName, Params: String; var Output: String): Integer;
var
  Lines: TExecOutput;
  I: Integer;
begin
  Output := '';
  if not ExecAndCaptureOutput(FileName, Params, '', SW_HIDE, ewWaitUntilTerminated, Result, Lines) then
  begin
    Output := SysErrorMessage(Result);
    Result := -1;
    exit;
  end;
  for I := 0 to GetArrayLength(Lines.StdOut) - 1 do
    Output := Output + Lines.StdOut[I] + #13#10;
end;

// Run a PowerShell command hidden, ignoring what it prints
procedure RunPowerShell(const Command: String);
var
  Output: String;
begin
  RunHidden(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), '-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "' + Command + '"', Output);
end;

// Stop the programs run from the installation, of every user, so their files can be replaced or deleted:
// the app, the daemon, and gdbus.exe, the session bus GLib starts for the app and leaves running after it
procedure StopPrograms;
begin
  RunPowerShell('Get-Process | Where-Object Path -like ''' + ExpandConstant('{app}') + '\*'' | Stop-Process -Force');
end;

// Remove the certificates of PSPdisp from the stores of the computer (also the ones of other builds, each build machine makes its own)
procedure RemoveCertificates;
begin
  RunPowerShell('Get-ChildItem Cert:\LocalMachine\Root, Cert:\LocalMachine\TrustedPublisher | Where-Object Subject -eq ''CN=PSPdisp'' | Remove-Item');
end;

// Trust the certificate that signs the driver packages: as a root (its chain) and as a trusted publisher (so Windows installs the drivers without asking)
function TrustCertificate: Boolean;
var
  Certificate, Output: String;
begin
  RemoveCertificates;
  Certificate := ExpandConstant('{app}\pspdisp-winsign.cer');
  Result := (RunHidden(ExpandConstant('{sys}\certutil.exe'), '-f -addstore Root "' + Certificate + '"', Output) = 0)
    and (RunHidden(ExpandConstant('{sys}\certutil.exe'), '-f -addstore TrustedPublisher "' + Certificate + '"', Output) = 0);
  if not Result then
    DriverErrors := DriverErrors + 'The certificate of the drivers wasn''t trusted:' + #13#10 + Output + #13#10;
end;

// Install or remove a driver package with pspdisp-driver.exe (see driver.c), noting what failed and if Windows has to restart
procedure Driver(const Action, Name, HardwareId: String);
var
  Params, Output: String;
  Code: Integer;
begin
  if Action = 'install' then
    Params := 'install "' + ExpandConstant('{app}\drivers\' + Name + '\pspdisp-' + Name + '.inf') + '"'
  else
    Params := 'uninstall pspdisp-' + Name + '.inf';
  if HardwareId <> '' then
    Params := Params + ' ' + HardwareId;

  Code := RunHidden(ExpandConstant('{app}\drivers\pspdisp-driver.exe'), Params, Output);
  if Code = 3010 then
    Restart := True
  else if Code <> 0 then
    DriverErrors := DriverErrors + 'The ' + Name + ' driver failed to ' + Action + ':' + #13#10 + Output + #13#10;
end;

// Install the driver of a component, or remove it when it's not selected (e.g. unchecked when installed again)
procedure DriverOfComponent(const Component, Name, HardwareId: String);
begin
  if WizardIsComponentSelected(Component) then
    Driver('install', Name, HardwareId)
  else
    Driver('uninstall', Name, HardwareId);
end;

// The firewall rule of the WLAN mode, for the daemon (the one of PSPdisp 0.6 had the same name, it's replaced)
procedure AllowWlan;
var
  Output: String;
begin
  RunHidden(ExpandConstant('{sys}\netsh.exe'), 'advfirewall firewall delete rule name="PSPdisp"', Output);
  RunHidden(ExpandConstant('{sys}\netsh.exe'), 'advfirewall firewall add rule name="PSPdisp" dir=in action=allow protocol=TCP localport={#WlanPorts} program="' + ExpandConstant('{app}\bin\pspdisp-daemon.exe') + '"', Output);
end;

function OldSettingsExist: Boolean;
begin
  Result := DirExists(ExpandConstant('{userappdata}\PSPdisp'));
end;

// Remove PSPdisp 0.6, which NSIS installed (in the 32-bit view of the registry), with its own uninstaller: it also removes its old drivers
procedure RemoveOldVersion;
var
  Uninstaller: String;
  Code: Integer;
begin
  if not RegQueryStringValue(HKLM32, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\PSPdisp', 'UninstallString', Uninstaller) then exit;
  Uninstaller := RemoveQuotes(Uninstaller);
  if not FileExists(Uninstaller) then exit;

  if SuppressibleMsgBox('PSPdisp 0.6 is installed, which this version replaces. Its uninstaller starts now, to remove it and its drivers first.', mbInformation, MB_OKCANCEL, IDOK) <> IDOK then exit;

  // _?= runs it from its folder, so it's waited for (it can't delete itself then, its folder is deleted after it)
  Exec(Uninstaller, '_?=' + ExtractFileDir(Uninstaller), '', SW_SHOW, ewWaitUntilTerminated, Code);
  if not RegValueExists(HKLM32, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\PSPdisp', 'UninstallString') then
    DelTree(ExtractFileDir(Uninstaller), True, True, True);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopPrograms;
  RemoveOldVersion;
  Result := '';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep <> ssPostInstall then exit;

  WizardForm.StatusLabel.Caption := 'Installing the drivers...';
  if TrustCertificate then
  begin
    DriverOfComponent('display', 'iddcx', 'Root\PSPdispIddCx');
    DriverOfComponent('gamepad', 'vhid', 'Root\PSPdispVhid');
    DriverOfComponent('winusb', 'winusb', '');
  end;

  if WizardIsTaskSelected('firewall') then
    AllowWlan;

  if DriverErrors <> '' then
    SuppressibleMsgBox('Some drivers of PSPdisp weren''t installed, the features they give won''t work:' + #13#10#13#10 + DriverErrors, mbError, MB_OK, IDOK);
end;

function NeedRestart: Boolean;
begin
  Result := Restart;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Output: String;
begin
  // Before the files are deleted, as the drivers are removed with pspdisp-driver.exe
  if CurUninstallStep <> usUninstall then exit;

  StopPrograms;
  Driver('uninstall', 'iddcx', 'Root\PSPdispIddCx');
  Driver('uninstall', 'vhid', 'Root\PSPdispVhid');
  Driver('uninstall', 'winusb', '');
  RemoveCertificates;
  RunHidden(ExpandConstant('{sys}\netsh.exe'), 'advfirewall firewall delete rule name="PSPdisp"', Output);

  if DriverErrors <> '' then
    SuppressibleMsgBox('Some drivers of PSPdisp weren''t removed:' + #13#10#13#10 + DriverErrors, mbError, MB_OK, IDOK);
end;

function UninstallNeedRestart: Boolean;
begin
  Result := Restart;
end;
