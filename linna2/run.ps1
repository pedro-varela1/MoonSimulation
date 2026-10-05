# LINNA-2 dataset: builds linna2.exe and runs the stages in order (see README.md). Finished frames are skipped, so a stopped run
# continues where it was. Examples:
#   .\run.ps1 -Sample                        the example in sample_dir
#   .\run.ps1                                the full dataset in output_dir
#   .\run.ps1 -Stages s1,check,manifest,qc   some stages only
# With -Sample, the bench stage also renders the first frames of the full S1 splits in their rendering order (sample_dir\bench),
# so qc.py estimates the full S1 from the throughput it will really have.
param(
    [switch]$Sample,
    [string[]]$Stages = @('regions', 'catalog', 'anglecheck', 'truth', 's1', 'clips', 'windows', 'check', 'manifest', 'repro', 'bench', 'parquet', 'qc'),
    [string]$Config = "$PSScriptRoot\config.ini",
    [string]$Python = "$env:LOCALAPPDATA\anaconda3\envs\linna-env\python.exe",
    [int]$Repro = 100,
    [int]$Bench = 500   # with -Sample: S1 frames per split rendered in the full run's order, for its throughput
)
$ErrorActionPreference = 'Stop'
$Stages = $Stages -split ','  # also "a,b,c" as one argument (powershell -File)
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
& mingw32-make -C (Split-Path $PSScriptRoot) -j8 linna2/linna2.exe
if ($LASTEXITCODE) { throw 'build failed' }

function Value($key) { (Select-String -Path $Config -Pattern "^\s*$key\s*=\s*([^#]*)" -Encoding UTF8).Matches[0].Groups[1].Value.Trim() }
$out = Value 'output_dir'
$root = if ($Sample) { Value 'sample_dir' } else { $out }
$flag = @(if ($Sample) { '--sample' })
$exe = "$PSScriptRoot\linna2.exe"
New-Item -ItemType Directory -Force "$root\logs" | Out-Null
$log = "$root\logs\run_$(Get-Date -Format yyyyMMdd_HHmmss).log"

foreach ($stage in $Stages) {
    "== $stage $(Get-Date -Format s)" | Tee-Object -FilePath $log -Append
    $t = Get-Date
    switch ($stage) {
        'regions' { if (-not (Test-Path "$out\splits\regions.json")) { & $Python "$PSScriptRoot\regions.py" $out | Tee-Object -FilePath $log -Append } }
        'catalog' { if (-not (Test-Path "$out\catalog\robbins_linna2.csv")) { & $exe $Config catalog | Tee-Object -FilePath $log -Append } }
        'parquet' { & $Python "$PSScriptRoot\to_parquet.py" $out $root | Tee-Object -FilePath $log -Append }
        'qc' { & $Python "$PSScriptRoot\qc.py" $root $Config | Tee-Object -FilePath $log -Append }
        'repro' { & $exe $Config repro @flag $Repro | Tee-Object -FilePath $log -Append }
        'bench' { if ($Sample) { & $exe $Config s1 --bench $Bench | Tee-Object -FilePath $log -Append } }
        default { & $exe $Config $stage @flag | Tee-Object -FilePath $log -Append }
    }
    if ($LASTEXITCODE) { throw "$stage failed (exit $LASTEXITCODE), log $log" }
    "{0}: {1:N0} s" -f $stage, ((Get-Date) - $t).TotalSeconds | Tee-Object -FilePath $log -Append
}
