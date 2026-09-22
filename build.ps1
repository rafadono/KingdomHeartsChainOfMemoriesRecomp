param(
    [string]$Target = "KHCOMRecomp",
    [string]$Config = "Release",
    [switch]$BuildRecompiler,
    [switch]$Recompile,
    [string]$RomPath = "roms\B8CE.gba"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

function Initialize-VcEnvironment {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }

    $vcvarsCandidates = @(
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    )

    $vcvars = $null
    foreach ($candidate in $vcvarsCandidates) {
        if (Test-Path -LiteralPath $candidate) {
            $vcvars = $candidate
            break
        }
    }

    if (-not $vcvars) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path -LiteralPath $vswhere) {
            $vsPath = & $vswhere -latest -products * -property installationPath
            if ($vsPath) {
                $possible = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
                if (Test-Path -LiteralPath $possible) { $vcvars = $possible }
            }
        }
    }

    if ($vcvars) {
        Write-Host "[KHCOMRecomp] Initializing MSVC x64 build environment..."
        cmd.exe /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
            if ($_ -match '^(.*?)=(.*)$') {
                [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], [System.EnvironmentVariableTarget]::Process)
            }
        }
    }
}

Initialize-VcEnvironment

function Find-Tool([string]$name, [string[]]$fallbackPaths) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($path in $fallbackPaths) {
        if (Test-Path -LiteralPath $path) { return $path }
    }
    return $null
}

$cmakeCandidates = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)

$cmakePath = Find-Tool "cmake.exe" $cmakeCandidates
if (-not $cmakePath) {
    throw "cmake.exe not found. Install Visual Studio with C++ CMake tools or add CMake to PATH."
}

$ninjaCandidates = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
)
$ninjaPath = Find-Tool "ninja.exe" $ninjaCandidates
if ($ninjaPath -and (-not (Get-Command ninja.exe -ErrorAction SilentlyContinue))) {
    $ninjaDir = Split-Path -Parent $ninjaPath
    $env:Path = "$ninjaDir;$env:Path"
}

$sdlHeader = Join-Path $root "third_party\sdl2\include\SDL.h"
if (-not (Test-Path -LiteralPath $sdlHeader)) {
    Write-Host "[KHCOMRecomp] Setting up SDL2 library..."
    $tempDir = Join-Path $root "third_party\temp"
    New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
    $zipPath = Join-Path $tempDir "sdl2.zip"
    curl.exe -L "https://github.com/libsdl-org/SDL/releases/download/release-2.30.12/SDL2-devel-2.30.12-VC.zip" -o $zipPath
    Expand-Archive -Path $zipPath -DestinationPath $tempDir -Force
    Move-Item -Path (Join-Path $tempDir "SDL2-2.30.12") -Destination (Join-Path $root "third_party\sdl2") -Force
    Remove-Item -Recurse -Force $tempDir
}

$recompUiDir = Join-Path $root "external\recomp-ui"
if (-not (Test-Path -LiteralPath (Join-Path $recompUiDir "CMakeLists.txt"))) {
    Write-Host "[KHCOMRecomp] Initializing recomp-ui submodule..."
    git submodule update --init --recursive external/recomp-ui
}

Write-Host "[KHCOMRecomp] CMake: $cmakePath"

$buildDir = Join-Path $root "build"

if ($BuildRecompiler) {
    Write-Host "[KHCOMRecomp] Building gba_recompile..."
    & $cmakePath -S $root -B $buildDir -G "Ninja Multi-Config"
    & $cmakePath --build $buildDir --config $Config --target gba_recompile --parallel
    Write-Host "[KHCOMRecomp] gba_recompile built at $buildDir\$Config\gba_recompile.exe"
    exit 0
}

if ($Recompile) {
    $recompilerBin = Join-Path $buildDir "gbarecomp_build\$Config\gba_recompile.exe"
    if (-not (Test-Path -LiteralPath $recompilerBin)) {
        Write-Host "[KHCOMRecomp] gba_recompile not found. Building it first..."
        & $PSCommandPath -BuildRecompiler -Config $Config
    }

    $resolvedRom = $null
    if ($RomPath -and (Test-Path -LiteralPath (Join-Path $root $RomPath))) {
        $resolvedRom = Join-Path $root $RomPath
    } else {
        $romCandidates = Get-ChildItem -Path (Join-Path $root "roms") -Filter "*.gba" -File -ErrorAction SilentlyContinue
        if ($romCandidates) {
            $resolvedRom = $romCandidates[0].FullName
        }
    }

    if (-not $resolvedRom -or -not (Test-Path -LiteralPath $resolvedRom)) {
        throw "ROM file not found. Place the ROM (.gba) into the roms/ directory."
    }

    Write-Host "[KHCOMRecomp] Executing static recompilation on $resolvedRom..."
    & $recompilerBin `
        --rom $resolvedRom `
        --config (Join-Path $root "config\b8ce.toml") `
        --symbols (Join-Path $root "symbols\imported_symbols.tsv") `
        --out (Join-Path $root "generated")

    Write-Host "[KHCOMRecomp] C++ code generated in generated/ directory."
    exit 0
}

Write-Host "[KHCOMRecomp] Configuring CMake in $buildDir..."
& $cmakePath -S $root -B $buildDir -G "Ninja Multi-Config"

Write-Host "[KHCOMRecomp] Compiling target '$Target' ($Config)..."
& $cmakePath --build $buildDir --config $Config --target $Target --parallel

$builtExe = Join-Path $buildDir "$Config\KHCOMRecomp.exe"
if (Test-Path -LiteralPath $builtExe) {
    Copy-Item -LiteralPath $builtExe -Destination (Join-Path $root "KHCOMRecomp.exe") -Force
    Write-Host "[KHCOMRecomp] Deployed KHCOMRecomp.exe to workspace root."
}

$builtToolchain = Join-Path $buildDir "$Config\overlay_toolchain"
if (Test-Path -LiteralPath $builtToolchain) {
    Copy-Item -Path $builtToolchain -Destination (Join-Path $root "overlay_toolchain") -Recurse -Force
    Write-Host "[KHCOMRecomp] Deployed overlay_toolchain to workspace root."
}

Write-Host "[KHCOMRecomp] Build completed successfully."
