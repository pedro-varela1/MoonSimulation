# Builds the release executable and the SelenITA installer.
# The data is taken from the paths in config.ini; only the files the simulator reads are packed.
# -Compact builds the ~5 GB variant without the high-resolution terrain, into output-compact. Its Setup.exe downloads the
# .bin slices it does not find next to it from -DownloadUrl (WebDAV of the public share https://cloud.cei.ita.br/s/6ac9HTioaAAMKxS),
# so the .bin files of each new compact build must be uploaded there.
param(
    [string]$Out = 'D:\LINNA\installer',
    [string]$Iscc = 'D:\LINNA\tools\InnoSetup6\ISCC.exe',
    [string]$Msys = 'C:\msys64',
    [string]$DownloadUrl = 'https://cloud.cei.ita.br/public.php/dav/files/6ac9HTioaAAMKxS',
    [switch]$Compact
)
$ErrorActionPreference = 'Stop'
if (Get-Process MoonSimulation -ErrorAction SilentlyContinue) { throw 'Close the simulator first: it keeps the executable and the SPICE kernels open' }
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$env:PATH = "$Msys\ucrt64\bin;$env:PATH"

# release build without a console window
Remove-Item MoonSimulation.exe -ErrorAction SilentlyContinue
& mingw32-make -j8 SUBSYSTEM=-mwindows
if ($LASTEXITCODE) { throw 'build failed' }

# a comment starts at a '#' that begins the line or follows a blank, as in config.h
$cfg = @{}
foreach ($line in [IO.File]::ReadAllLines("$root\config.ini", [Text.Encoding]::UTF8)) {
    if ($line -match '^\s*(\w+)\s*=(.*)$') { $cfg[$Matches[1]] = ($Matches[2] -replace '(^|\s)#.*$', '').Trim() }
}
function Source($key) {
    $p = $cfg[$key]
    if (-not [IO.Path]::IsPathRooted($p)) { $p = Join-Path $root $p }
    (Resolve-Path -LiteralPath $p).Path
}

$stage = "$Out\stage"
$output = if ($Compact) { "$Out\output-compact" } else { "$Out\output" }
Remove-Item $stage, $output -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory "$stage\assets", "$stage\licenses" | Out-Null
Copy-Item MoonSimulation.exe, shaders $stage -Recurse
Copy-Item assets\fonts, assets\logos "$stage\assets" -Recurse

# orbit files: the configured one plus those listed in orbits.txt
$orbits = @(Source orbit_file) + (Get-Content "$PSScriptRoot\orbits.txt" | Where-Object { $_.Trim() -and -not $_.StartsWith('#') }) |
    ForEach-Object { (Resolve-Path -LiteralPath $_).Path } | Sort-Object -Unique
New-Item -ItemType Directory "$stage\data\orbits" | Out-Null
Copy-Item -LiteralPath $orbits "$stage\data\orbits"

# CubeSat model with its material libraries
if ($cfg.obj_file) {
    $obj = Source obj_file
    New-Item -ItemType Directory "$stage\data\model" | Out-Null
    Copy-Item -LiteralPath $obj "$stage\data\model"
    foreach ($line in [IO.File]::ReadLines($obj)) {
        if ($line -match '^mtllib\s+(.+)$') { Copy-Item -LiteralPath (Join-Path (Split-Path $obj) $Matches[1].Trim()) "$stage\data\model" }
    }
}

# MSYS2 runtime DLLs the executable loads, with their licenses
$dlls = & "$Msys\usr\bin\ldd.exe" MoonSimulation.exe | Select-String '/ucrt64/bin/(\S+)' | ForEach-Object { $_.Matches[0].Groups[1].Value }
foreach ($d in $dlls) { Copy-Item "$Msys\ucrt64\bin\$d" $stage }
Copy-Item third_party\imgui\LICENSE.txt "$stage\licenses\imgui.txt"
foreach ($p in 'gcc-libs', 'glew', 'glfw', 'jbigkit', 'lerc', 'libdeflate', 'libjpeg-turbo', 'libpng', 'libtiff', 'libwebp', 'libwinpthread', 'xz', 'zlib', 'zstd') {
    Copy-Item "$Msys\ucrt64\share\licenses\$p" "$stage\licenses\$p" -Recurse
}

# config.ini with the data paths of the installed layout (see selenita.iss) and automatic quality
$installed = @{
    orbit_file = 'data/orbits/' + (Split-Path (Source orbit_file) -Leaf); spice_dir = 'data/spice'; sldem_dir = 'data/sldem512'
    polar_dir = 'data/lola_polar'; wac_643 = 'data/wac/643'; wac_566 = 'data/wac/566'; wac_415 = 'data/wac/415'; hapke_dir = 'data/hapke'
    kit_color = 'data/global/' + (Split-Path (Source kit_color) -Leaf); lola_dem = 'data/global/' + (Split-Path (Source lola_dem) -Leaf)
    star_catalog = 'data/stars/' + (Split-Path (Source star_catalog) -Leaf); earth_texture = 'data/earth/' + (Split-Path (Source earth_texture) -Leaf)
    crater_db = 'data/craters/' + (Split-Path (Source crater_db) -Leaf); quality = 'auto'
}
if ($cfg.obj_file) { $installed.obj_file = 'data/model/' + (Split-Path (Source obj_file) -Leaf) }
$lines = [IO.File]::ReadAllLines("$root\config.ini", [Text.Encoding]::UTF8) | ForEach-Object {
    if ($_ -match '^(\s*(\w+)\s*=\s*)(.*?)((\s+#.*)?)$' -and $installed.ContainsKey($Matches[2])) { $Matches[1] + $installed[$Matches[2]] + $Matches[4] } else { $_ }
}
if (-not ($lines -match '^\s*quality\s*=')) { $lines += 'quality       = auto' }
[IO.File]::WriteAllText("$stage\config.ini", ($lines -join "`n") + "`n", (New-Object Text.UTF8Encoding $false))

# README.txt and the MIT license, installed with the program and copied next to the installer
function Save($path, $text) { [IO.File]::WriteAllText($path, ($text.Trim() -replace "`r?`n", "`r`n") + "`r`n", (New-Object Text.UTF8Encoding $true)) }
$version = [regex]::Match((Get-Content "$PSScriptRoot\selenita.iss" -Raw), '#define AppVersion "(.+?)"').Groups[1].Value
$setup = "SelenITA-Setup-$version" + $(if ($Compact) { '-Compact' } else { '' })
$ita = "Instituto Tecnol$([char]0xF3)gico de Aeron$([char]0xE1)utica (ITA)"
if ($Compact) {
    $variant = 'compact installer'
    $images = ''
    $about = @'
This is the compact installer (about 5.5 GB installed). It leaves out the high-resolution terrain (SLDEM2015 and LOLA
polar, about 38 GB): the camera view uses the global LOLA terrain (about 470 m per pixel), and the telemetry lists the
high-resolution terrain as not installed. Running the full installer later over this installation adds it.
'@
    $disk = 'about 5.5 GB, plus about 4 GB while the data files are downloaded'
    $pages = 'choose the folder and whether to create a desktop shortcut'
    $get = @"
1. Only $setup.exe is needed. When the $setup-*.bin data files are not next to it, Setup downloads
   them (about 4 GB) from the SelenITA server before installing, which needs an internet connection and write access
   to the installer's folder, and deletes them once the installation succeeds. With the .bin files next to the .exe,
   it installs without internet.
"@
} else {
    $variant = 'full installer'
    $about = @'
This is the full installer. Its Full installation (about 44 GB) includes the high-resolution terrain of the camera view:
SLDEM2015 512 ppd (about 59 m per pixel) up to 60 degrees of latitude and LOLA 60 m above. The Compact installation
(about 5.5 GB) leaves it out and uses the global LOLA terrain (about 470 m per pixel); Custom picks the parts. The
Full installation also brings the Robbins (2018) crater catalogue, which enables the GENERATE IMAGES button.
'@
    $images = @'

   - GENERATE IMAGES saves one camera image per orbit file row, from the start time to a chosen end time, into the
     img, json and label subfolders of a chosen folder: the PNG as the camera shows it, a JSON with the camera (and
     its exposure), the Sun and the Earth, and the labels of the catalogue craters in view (class 0, conic or centre + radius).
'@
    $disk = 'about 44 GB for the Full installation, about 5.5 GB for the Compact one'
    $pages = 'choose the folder, the installation type (Full, Compact or Custom) and whether to create a desktop shortcut'
    $get = @"
1. Keep $setup.exe and all the $setup-*.bin files in the same folder. To take them to another
   computer, copy (or zip) the whole folder.
"@
}
Save "$stage\README.txt" @"
SelenITA Lunar Orbit Simulator $version - $variant
==================================================

Real-time simulator of the SelenITA CubeSat in lunar orbit. The orbit view shows the Moon lit by the real Sun, the
satellite and its track; the camera view shows what the CubeSat camera sees, with the real terrain, color, shadows and
earthshine.

$about

REQUIREMENTS
- Windows 10 or 11, 64-bit.
- A graphics card with OpenGL 4.6 and an up-to-date driver (NVIDIA, AMD or Intel). Virtual machines, remote desktop
  sessions and the basic Microsoft display driver do not provide it.
- Free disk space: $disk.
- Nothing else: the installer carries every library and data file the simulator needs.

INSTALL
$get
2. Run $setup.exe. No administrator rights are needed: it installs for the current Windows user, by
   default in %LOCALAPPDATA%\Programs\SelenITA.
3. Accept the MIT license, $pages.
4. Launch the simulator at the end, or later from the Start menu (SelenITA Lunar Orbit Simulator) or the desktop shortcut.

RUN
1. Start screen: choose the orbit and the simulation settings.
   - $($orbits.Count) SelenITA orbit files come in data\orbits; Browse... opens any other STK report.
   - The start time must be inside the file's time range.
   - Set the orbit view FOV, the camera FOV, tilt, azimuth and roll, the color mode, earthshine and the quality
     (Auto chooses it from the video memory).
   - START SIMULATION is enabled once every field is valid.
   - GENERATE ORBIT REPORT instead saves a CSV with one line per orbit file row: time, satellite, Sun and camera.$images
2. After loading (up to about a minute the first time), the simulation fills the screen:
   - Orbit view (left): drag to rotate, scroll to zoom. Its boxes mark the poles, show the Moon axes and the camera
     field of view; the Satellite slider enlarges the satellite.
   - Camera view (right), with the telemetry below it.
   - Bottom bar: timeline, time jumps, pause, speed, follow the satellite, reset the view, Settings (back to the start
     screen) and Quit.
3. The start screen values are saved in config.ini, in the installation folder. The other settings (camera sensor,
   satellite model, orbit view gain) are edited in that file.

ORBIT FILES
STK reports with the columns Time (UTCG), LatitudeMoon (deg), LongitudeMoon (deg) and AltitudeMoon (km) after the line
of dashes, at any time step. Their latitude and longitude are MOON_PA or MOON_ME, as chosen on the start screen.

IF IT DOES NOT START
- "needs a graphics card with OpenGL 4.6": update the graphics driver. On laptops with two graphics cards, make sure
  Windows runs the simulator on the dedicated one.
- "not enough video memory": choose a lower Quality on the start screen.

UNINSTALL
Windows Settings > Apps > Installed apps > SelenITA Lunar Orbit Simulator > Uninstall.

LICENSE
The simulator is released under the MIT License (LICENSE.txt). The bundled libraries keep their own licenses (licenses
folder). Data: NASA LRO LOLA, SLDEM2015 and LROC WAC products, NASA Blue Marble, NAIF SPICE kernels and the Hipparcos
catalog.
"@
Save "$stage\LICENSE.txt" @"
MIT License

Copyright (c) 2026 SelenITA team, $ita

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

The bundled third-party libraries keep their own licenses, in the licenses folder.
"@

$defines = @("/DStage=$stage", "/DOutputDir=$output", "/DSpiceDir=$(Source spice_dir)", "/DStarCatalog=$(Source star_catalog)",
    "/DEarthTexture=$(Source earth_texture)", "/DLolaDem=$(Source lola_dem)", "/DKitColor=$(Source kit_color)", "/DHapkeDir=$(Source hapke_dir)",
    "/DWac643=$(Source wac_643)", "/DWac566=$(Source wac_566)", "/DWac415=$(Source wac_415)")
$defines += if ($Compact) { '/DCompact', "/DDownloadUrl=$DownloadUrl" } else { "/DSldemDir=$(Source sldem_dir)", "/DPolarDir=$(Source polar_dir)", "/DCraterDb=$(Source crater_db)" }
& $Iscc @defines "$PSScriptRoot\selenita.iss"
if ($LASTEXITCODE) { throw 'installer compilation failed' }
Copy-Item "$stage\README.txt", "$stage\LICENSE.txt" $output
