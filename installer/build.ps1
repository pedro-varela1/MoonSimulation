# Builds the release executable and the SelenITA installer.
# The data is taken from the paths in config.ini; only the files the simulator reads are packed.
# -Compact builds the ~5 GB variant without the high-resolution terrain, into output-compact.
param(
    [string]$Out = 'D:\LINNA\installer',
    [string]$Iscc = 'D:\LINNA\tools\InnoSetup6\ISCC.exe',
    [string]$Msys = 'C:\msys64',
    [switch]$Compact
)
$ErrorActionPreference = 'Stop'
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
foreach ($p in 'gcc-libs', 'glew', 'glfw', 'jbigkit', 'lerc', 'libdeflate', 'libjpeg-turbo', 'libtiff', 'libwebp', 'libwinpthread', 'xz', 'zlib', 'zstd') {
    Copy-Item "$Msys\ucrt64\share\licenses\$p" "$stage\licenses\$p" -Recurse
}

# config.ini with the data paths of the installed layout (see selenita.iss) and automatic quality
$installed = @{
    orbit_file = 'data/orbits/' + (Split-Path (Source orbit_file) -Leaf); spice_dir = 'data/spice'; sldem_dir = 'data/sldem512'
    polar_dir = 'data/lola_polar'; wac_643 = 'data/wac/643'; wac_566 = 'data/wac/566'; wac_415 = 'data/wac/415'; hapke_dir = 'data/hapke'
    kit_color = 'data/global/' + (Split-Path (Source kit_color) -Leaf); lola_dem = 'data/global/' + (Split-Path (Source lola_dem) -Leaf)
    star_catalog = 'data/stars/' + (Split-Path (Source star_catalog) -Leaf); earth_texture = 'data/earth/' + (Split-Path (Source earth_texture) -Leaf)
    quality = 'auto'
}
if ($cfg.obj_file) { $installed.obj_file = 'data/model/' + (Split-Path (Source obj_file) -Leaf) }
$lines = [IO.File]::ReadAllLines("$root\config.ini", [Text.Encoding]::UTF8) | ForEach-Object {
    if ($_ -match '^(\s*(\w+)\s*=\s*)(.*?)((\s+#.*)?)$' -and $installed.ContainsKey($Matches[2])) { $Matches[1] + $installed[$Matches[2]] + $Matches[4] } else { $_ }
}
if (-not ($lines -match '^\s*quality\s*=')) { $lines += 'quality       = auto' }
[IO.File]::WriteAllText("$stage\config.ini", ($lines -join "`n") + "`n", (New-Object Text.UTF8Encoding $false))

$defines = @("/DStage=$stage", "/DOutputDir=$output", "/DSpiceDir=$(Source spice_dir)", "/DStarCatalog=$(Source star_catalog)",
    "/DEarthTexture=$(Source earth_texture)", "/DLolaDem=$(Source lola_dem)", "/DKitColor=$(Source kit_color)", "/DHapkeDir=$(Source hapke_dir)",
    "/DWac643=$(Source wac_643)", "/DWac566=$(Source wac_566)", "/DWac415=$(Source wac_415)")
$defines += if ($Compact) { '/DCompact' } else { "/DSldemDir=$(Source sldem_dir)", "/DPolarDir=$(Source polar_dir)" }
& $Iscc @defines "$PSScriptRoot\selenita.iss"
if ($LASTEXITCODE) { throw 'installer compilation failed' }
