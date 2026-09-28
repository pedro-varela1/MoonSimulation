; SelenITA Lunar Orbit Simulator installer (Inno Setup 6).
; Built by build.ps1, which stages the program and passes the data paths read from config.ini.
; /DCompact builds the variant without the high-resolution terrain (SLDEM and polar DEM); both install the same app.

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
OutputBaseFilename=SelenITA-Setup-{#AppVersion}{#Variant}
SetupIconFile=selenita.ico
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
Name: "core"; Description: "Simulator, global terrain and color, sky and the SelenITA orbit"; Types: full compact custom; Flags: fixed
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
