<#
.SYNOPSIS
  Headless capture run for KHCOMRecomp.exe (see docs/CAPTURE_HARNESS.md).
.EXAMPLE
  .\tools\capture\run_capture.ps1 -Dir captures\t1 -Frames "300,600-610" -Script tools\capture\scripts\first_dialogue.txt `
      -Window 852x480 -Set "screen_mask=0;hdtext_enabled=1;hdtext_family=0;hdtext_size=100" -Mode output -Fast
#>
param(
    [string]$Dir = "captures\run",          # output directory (PNG files)
    [string]$Frames = "",                   # e.g. "300,600-610,1200" (emulated frames, see doc)
    [string]$Script = "",                   # input script file or inline "120-125: A; 200: START"
    [string]$Set = "",                      # "key=value;key=value" (khcom_config.ini keys)
    [string]$Window = "",                   # "WxH" output size, e.g. 852x480
    [string]$Variants = "",                 # "852x480 screen_mask=0|960x640" : one output image per entry at each captured frame
    [ValidateSet("output","native","both")][string]$Mode = "output",
    [string]$LoadState = "",                # savestate file to load (KHCOM_SAVESTATE_LOAD)
    [string]$SaveState = "",                # "<frame>:<slot>" (KHCOM_SAVESTATE_SAVE)
    [switch]$Fast,                          # 4x fast-forward (KHCOM_FAST=1) -- captures may lag by up to 3 frames
    [switch]$Sharp,                         # pass --sharp-filter 1 (integer prescale + linear finish, the launcher default; off in a bare --rom run)
    [string]$ExtraArgs = "",                # extra command-line arguments for the game
    [int]$ExitFrame = 0,                    # quit at this emulated frame even without captures
    [int]$TimeoutSec = 240,
    [string]$Exe = "KHCOMRecomp.exe",
    [string]$Rom = "roms\Kingdom Hearts - Chain of Memories (U)(Venom).gba",
    [string]$Bios = "bios\gba (W).gba",
    [string]$Log = ""
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $root
if (-not (Test-Path $Exe)) { throw "$Exe not found in $root (build it with .\build.ps1 -BuildDir build-vs)" }
New-Item -ItemType Directory -Force -Path $Dir | Out-Null
$absDir = (Resolve-Path $Dir).Path

$env:SDL_VIDEODRIVER = "dummy"; $env:SDL_AUDIODRIVER = "dummy"
$env:KHCOM_CAPTURE_DIR = $absDir
$env:KHCOM_CAPTURE_MODE = $Mode
if ($Frames) { $env:KHCOM_CAPTURE_FRAMES = $Frames; $env:KHCOM_EXIT_AFTER_CAPTURE = "1" }
if ($Script) {
    if (Test-Path $Script) { $env:KHCOM_INPUT_SCRIPT = (Resolve-Path $Script).Path } else { $env:KHCOM_INPUT_SCRIPT = $Script }
}
if ($Set) { $env:KHCOM_SET = $Set }
if ($Window) { $env:KHCOM_WINDOW_SIZE = $Window }
if ($Variants) { $env:KHCOM_CAPTURE_VARIANTS = $Variants }
if ($LoadState) { $env:KHCOM_SAVESTATE_LOAD = (Resolve-Path $LoadState).Path }
if ($SaveState) { $env:KHCOM_SAVESTATE_SAVE = $SaveState }
if ($Fast) { $env:KHCOM_FAST = "1" }
if ($ExitFrame -gt 0) { $env:KHCOM_EXIT_FRAME = "$ExitFrame" }

if (-not $Log) { $Log = Join-Path $absDir "run.log" }
$errLog = "$Log.err"
$argStr = "--rom `"$Rom`" --bios `"$Bios`""
if ($Sharp) { $argStr += " --sharp-filter 1" }
if ($ExtraArgs) { $argStr += " $ExtraArgs" }
$p = Start-Process -FilePath (Join-Path $root $Exe) -ArgumentList $argStr -WorkingDirectory $root `
    -RedirectStandardOutput $Log -RedirectStandardError $errLog -PassThru -WindowStyle Hidden
if (-not $p.WaitForExit($TimeoutSec * 1000)) {
    Write-Host "timeout after ${TimeoutSec}s, killing PID $($p.Id)"
    $p.Kill(); $p.WaitForExit()
}
Write-Host "exit code: $($p.ExitCode)  (logs: $Log, $errLog)"
Get-ChildItem $absDir -Filter *.png | Sort-Object Name | ForEach-Object { Write-Host $_.FullName }
