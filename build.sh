#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR"
BUILD_DIR="$ROOT/build"
CONFIG="Release"
RECOMPILE=false
BUILD_RECOMPILER=false

show_help() {
    cat <<EOF
KHCOMRecomp Linux Build Script

Usage:
  ./build.sh [options]

Options:
  -c, --config <Config>     Build configuration: Release (default) or Debug
  -r, --recompile           Run gba_recompile on the ROM to regenerate C++ sources
  --build-recompiler        Build gba_recompile binary only
  -h, --help                Show this help message

Distro Dependencies:
  Fedora:
    sudo dnf install gcc-c++ cmake ninja-build SDL2-devel
  Ubuntu / Debian:
    sudo apt install build-essential cmake ninja-build libsdl2-dev
  Arch Linux:
    sudo pacman -S base-devel cmake ninja sdl2
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--config)
            CONFIG="$2"
            shift 2
            ;;
        -r|--recompile)
            RECOMPILE=true
            shift
            ;;
        --build-recompiler)
            BUILD_RECOMPILER=true
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            echo "Unknown argument: $1"
            show_help
            exit 1
            ;;
    esac
done

# Check essential dependencies
for tool in cmake; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Error: $tool is not installed."
        echo "Please install dependencies as shown in './build.sh --help'"
        exit 1
    fi
done

GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
    GENERATOR="Ninja"
fi

echo "[KHCOMRecomp] Generator: $GENERATOR"
echo "[KHCOMRecomp] Build Type: $CONFIG"

if [ "$BUILD_RECOMPILER" = true ]; then
    echo "[KHCOMRecomp] Building gba_recompile..."
    cmake -S "$ROOT" -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE="$CONFIG"
    cmake --build "$BUILD_DIR" --config "$CONFIG" --target gba_recompile --parallel
    echo "[KHCOMRecomp] gba_recompile built successfully."
    exit 0
fi

if [ "$RECOMPILE" = true ]; then
    RECOMPILER_BIN="$BUILD_DIR/gbarecomp_build/tools/gba_recompile/gba_recompile"
    if [ ! -f "$RECOMPILER_BIN" ]; then
        echo "[KHCOMRecomp] gba_recompile not found. Building it first..."
        "$0" --build-recompiler -c "$CONFIG"
    fi

    ROM_PATH=""
    for candidate in "$ROOT"/roms/*.gba; do
        if [ -f "$candidate" ]; then
            ROM_PATH="$candidate"
            break
        fi
    done

    if [ -z "$ROM_PATH" ]; then
        echo "Error: No .gba ROM found in roms/ directory."
        exit 1
    fi

    echo "[KHCOMRecomp] Executing static recompilation on $ROM_PATH..."
    "$RECOMPILER_BIN" \
        --rom "$ROM_PATH" \
        --config "$ROOT/config/b8ce.toml" \
        --symbols "$ROOT/symbols/imported_symbols.tsv" \
        --out "$ROOT/generated"

    echo "[KHCOMRecomp] C++ code generated in generated/ directory."
    exit 0
fi

echo "[KHCOMRecomp] Configuring CMake in $BUILD_DIR..."
cmake -S "$ROOT" -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE="$CONFIG"

echo "[KHCOMRecomp] Compiling target 'KHCOMRecomp' ($CONFIG)..."
cmake --build "$BUILD_DIR" --config "$CONFIG" --target KHCOMRecomp --parallel

BUILT_BIN="$BUILD_DIR/KHCOMRecomp"
if [ -f "$BUILT_BIN" ]; then
    cp -f "$BUILT_BIN" "$ROOT/KHCOMRecomp"
    chmod +x "$ROOT/KHCOMRecomp"
    echo "[KHCOMRecomp] Deployed native Linux binary 'KHCOMRecomp' to workspace root."
fi

echo "[KHCOMRecomp] Build completed successfully."
