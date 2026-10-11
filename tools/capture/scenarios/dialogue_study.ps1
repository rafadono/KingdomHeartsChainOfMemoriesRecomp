<#
  Visual study of the first dialogue page: HD text overlay (font family / size) vs. the original text, at several
  output scales and with the screen mask on/off. One full run per configuration (no savestate). At frame 10500
  (page fully typed, no more input) the harness takes the native image plus one output image per variant (window
  size x screen mask) via KHCOM_CAPTURE_VARIANTS. Runs are independent processes, so $Parallel of them run at the
  same time. -Sharp passes --sharp-filter 1 (the launcher default: integer prescale + linear finish); -Only limits
  the configurations (comma separated names, e.g. authentic,fam0_100).
  Configuration names: authentic (original text), fam<index>_<size> where <index> is the font family index
  (0 = first built-in family, see the in-game menu) and <size> the size in tenths of a GBA pixel (100 = 10 px).
  Results: captures\dialogue_study\<cfg>\f010500_output_<variant>_WxH.png (+ _native_)
#>
param([string]$Out = "captures\dialogue_study", [int]$Parallel = 4, [switch]$Sharp, [string]$Only = "")
$onlyList = @($Only -split "," | Where-Object { $_ })
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
Set-Location $root
New-Item -ItemType Directory -Force -Path $Out | Out-Null

$wins = "852x480", "960x640", "1536x864", "1440x960"
$full = @(); foreach ($m in 1, 0) { foreach ($w in $wins) { $full += "$w screen_mask=$m" } }
$full = $full -join "|"
$short = "960x640 screen_mask=0|1536x864 screen_mask=0"

$base = "perf_mode=0"
$cfgs = [ordered]@{ "authentic" = @("$base;hdtext_enabled=0", $full) }
foreach ($fam in 0..7) {
    foreach ($sz in 80, 100, 120) {
        $k = "fam${fam}_$sz"
        $cfgs[$k] = @("$base;hdtext_enabled=1;hdtext_family=$fam;hdtext_size=$sz", $(if ($sz -eq 100) { $full } else { $short }))
    }
}

$jobs = @()
foreach ($k in @($cfgs.Keys)) {
    if ($onlyList.Count -gt 0 -and $onlyList -notcontains $k) { continue }
    while (@($jobs | Where-Object { $_.State -eq "Running" }).Count -ge $Parallel) { Start-Sleep -Seconds 2 }
    $dir = Join-Path $Out $k
    $set = "perf_mode=0;" + $cfgs[$k][0]
    $var = $cfgs[$k][1]
    $runner = Join-Path $PSScriptRoot "..\run_capture.ps1"
    $jobs += Start-Job -Name $k -ScriptBlock {
        param($runner, $dir, $set, $var, $root, $sharp)
        Set-Location $root
        & $runner -Dir $dir -Frames "10500" -Mode both -Set $set -Variants $var `
            -Script "tools\capture\scripts\first_dialogue.txt" -Fast -TimeoutSec 400 -Sharp:$sharp | Out-Null
    } -ArgumentList $runner, $dir, $set, $var, $root, ([bool]$Sharp)
    Write-Host "started $k"
    Start-Sleep -Seconds 3
}
$jobs | Wait-Job | Out-Null
$jobs | Remove-Job
Write-Host "all done"
