; SelenITA Lunar Orbit Simulator installer (Inno Setup 6).
; Built by build.ps1, which stages the program and passes the data paths read from config.ini.
; /DCompact builds the variant without the high-resolution terrain (SLDEM and polar DEM); both install the same app.
; The compact Setup.exe also works alone: it downloads its .bin slices from /DDownloadUrl when they are not next to it.

#define AppName "SelenITA Lunar Orbit Simulator"
#define AppVersion "1.0.0"
#define Exe "MoonSimulation.exe"
#ifdef Compact
  #define Variant "-Compact"
  #define Core ""
#else
  #define Variant ""
  #define Core "; Components: core"
#endif
#define SetupName "SelenITA-Setup-" + AppVersion + Variant

[Setup]
AppId={{9C1D5E7A-3B2F-4E8A-9A61-5F0C2D7B8E14}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=SelenITA
DefaultDirName={autopf}\SelenITA
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename={#SetupName}
SetupIconFile=selenita.ico
LicenseFile={#Stage}\LICENSE.txt
UninstallDisplayIcon={app}\{#Exe}
UninstallDisplayName={#AppName}
WizardStyle=modern
Compression=lzma2/fast
LZMAUseSeparateProcess=yes
LZMANumBlockThreads=8
DiskSpanning=yes
; 2 GB parts, so the installer fits any drive or file system (FAT32 limits files to 4 GB)
DiskSliceSize=2000000000

#ifndef Compact
[Types]
Name: "full"; Description: "Full installation (high-resolution terrain)"
Name: "compact"; Description: "Compact installation (global terrain only)"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "core"; Description: "Simulator, global terrain and color, sky and the SelenITA orbits"; Types: full compact custom; Flags: fixed
Name: "sldem"; Description: "High-resolution terrain: SLDEM2015 512 ppd, latitudes up to 60 degrees"; Types: full
Name: "polar"; Description: "High-resolution polar terrain: LOLA 60 m, latitudes above 60 degrees"; Types: full
#endif

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Excludes: "config.ini"; Flags: recursesubdirs ignoreversion{#Core}
Source: "{#Stage}\config.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist{#Core}
Source: "{#SpiceDir}\*"; DestDir: "{app}\data\spice"{#Core}
Source: "{#StarCatalog}"; DestDir: "{app}\data\stars"{#Core}
Source: "{#EarthTexture}"; DestDir: "{app}\data\earth"{#Core}
Source: "{#LolaDem}"; DestDir: "{app}\data\global"{#Core}
Source: "{#KitColor}"; DestDir: "{app}\data\global"{#Core}
Source: "{#HapkeDir}\WAC_HAPKEPARAMMAP_643NM.IMG"; DestDir: "{app}\data\hapke"{#Core}
Source: "{#HapkeDir}\WAC_HAPKEPARAMMAP_566NM.IMG"; DestDir: "{app}\data\hapke"{#Core}
Source: "{#HapkeDir}\WAC_HAPKEPARAMMAP_415NM.IMG"; DestDir: "{app}\data\hapke"{#Core}
Source: "{#Wac643}\WAC_HAPKE_643NM_E350*.IMG"; DestDir: "{app}\data\wac\643"{#Core}
Source: "{#Wac566}\WAC_HAPKE_566NM_E350*.IMG"; DestDir: "{app}\data\wac\566"{#Core}
Source: "{#Wac415}\WAC_HAPKE_415NM_E350*.IMG"; DestDir: "{app}\data\wac\415"{#Core}
#ifndef Compact
; already DEFLATE compressed
Source: "{#SldemDir}\sldem2015_512_*_float.tif"; DestDir: "{app}\data\sldem512"; Flags: nocompression; Components: sldem
Source: "{#PolarDir}\ldem_60n_60m.img"; DestDir: "{app}\data\lola_polar"; Components: polar
Source: "{#PolarDir}\ldem_60s_60m.img"; DestDir: "{app}\data\lola_polar"; Components: polar
#endif

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#Exe}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#Exe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#Exe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

#ifdef Compact
[Code]
// Setup reads its data slices next to itself. Before installing, the slices missing there, or of another size than on the
// server, are downloaded; they are deleted after a successful installation. Without the server, the local slices are used.
var
  DownloadPage: TDownloadWizardPage;
  Fetched: TStringList;
  Installed: Boolean;

function Slice(N: Integer): String;
begin
  Result := '{#SetupName}-' + IntToStr(N) + '.bin';
end;

function LocalSize(const Name: String): Int64;
begin
  if not FileSize64(ExpandConstant('{src}\') + Name, Result) then Result := -1;
end;

// -1 when the server does not have the file (HTTP 404); any other failure raises
function RemoteSize(const Name: String): Int64;
begin
  try
    Result := DownloadTemporaryFileSize('{#DownloadUrl}/' + Name);
  except
    if Pos('404', GetExceptionMessage) = 0 then RaiseException(GetExceptionMessage);
    Result := -1;
  end;
end;

function Room(const Path: String; Need: Int64): Boolean;
var
  Free, Total: Int64;
begin
  Result := not GetSpaceOnDisk64(Path, Free, Total) or (Free >= Need);
end;

// from {tmp}, where downloads land, to the folder of Setup.exe
function Place(const Name: String): Boolean;
var
  From, Dest: String;
begin
  From := ExpandConstant('{tmp}\') + Name;
  Dest := ExpandConstant('{src}\') + Name;
  DeleteFile(Dest);
  Result := RenameFile(From, Dest) or (CopyFile(From, Dest, False) and DeleteFile(From));
end;

procedure InitializeWizard;
begin
  DownloadPage := CreateDownloadPage('Downloading the installation data', 'Setup is downloading the simulator data from the SelenITA server. This may take a while.', nil);
  DownloadPage.ShowBaseNameInsteadOfUrl := True;
  Fetched := TStringList.Create;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  N: Integer;
  Size, Need: Int64;
  Error: String;
begin
  Result := True;
  if CurPageID <> wpReady then Exit;
  DownloadPage.Clear;
  Fetched.Clear;
  Need := 0;
  N := 1;
  Error := '';
  try
    Size := RemoteSize(Slice(1));
    while Size >= 0 do begin
      if LocalSize(Slice(N)) <> Size then begin
        DownloadPage.Add('{#DownloadUrl}/' + Slice(N), Slice(N), '');
        Fetched.Add(Slice(N));
        Need := Need + Size;
      end;
      N := N + 1;
      Size := RemoteSize(Slice(N));
    end;
    if N = 1 then Error := 'The installation data was not found on the SelenITA server';
  except
    Error := 'The SelenITA server could not be reached. ' + GetExceptionMessage;
  end;
  if Error <> '' then begin
    Result := LocalSize(Slice(1)) >= 0;
    if not Result then
      SuppressibleMsgBox(AddPeriod(Error) + #13#10#13#10 + 'Check the internet connection, or place the {#SetupName}-*.bin files next to this installer, and try again.', mbCriticalError, MB_OK, IDOK);
    Exit;
  end;
  if Fetched.Count = 0 then Exit;
  if not (Room(ExpandConstant('{tmp}'), Need) and Room(ExpandConstant('{src}'), Need)) then begin
    SuppressibleMsgBox('Downloading the installation data needs ' + IntToStr(Need div 1000000000 + 1) + ' GB of free space on the drive of this installer and on the drive of the temporary folder.', mbCriticalError, MB_OK, IDOK);
    Result := False;
    Exit;
  end;
  DownloadPage.Show;
  try
    try
      DownloadPage.Download;
      for N := 0 to Fetched.Count - 1 do
        if not Place(Fetched[N]) then
          RaiseException('Could not save ' + Fetched[N] + ' in ' + ExpandConstant('{src}') + '. Copy the installer to a folder you can write to, such as Downloads, and run it again');
    except
      if not DownloadPage.AbortedByUser then
        SuppressibleMsgBox(AddPeriod(GetExceptionMessage), mbCriticalError, MB_OK, IDOK);
      Result := False;
    end;
  finally
    DownloadPage.Hide;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  Installed := Installed or (CurStep = ssDone);
end;

function Quoted(const Name: String): String;
begin
  Result := ExpandConstant('{src}\') + Name;
  StringChangeEx(Result, '''', '''''', True);
  Result := '''' + Result + '''';
end;

// Setup keeps the last slice open until it ends, so a hidden PowerShell deletes the downloaded slices once it can
procedure DeinitializeSetup;
var
  I, Code: Integer;
  Files: String;
begin
  if not Installed then Exit;
  if Fetched.Count = 0 then Exit;
  Files := Quoted(Fetched[0]);
  for I := 1 to Fetched.Count - 1 do
    Files := Files + ',' + Quoted(Fetched[I]);
  Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), '-NoProfile -WindowStyle Hidden -Command "for ($i = 0; $i -lt 120 -and ((Test-Path ' +
    Files + ') -contains $true); $i++) { Remove-Item ' + Files + ' -ErrorAction SilentlyContinue; Start-Sleep 1 }"', '', SW_HIDE, ewNoWait, Code);
end;
#endif
