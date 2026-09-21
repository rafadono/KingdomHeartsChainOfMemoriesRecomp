#!/usr/bin/env python3
import sys
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMPORTER = ROOT / "gbarecomp" / "tools" / "symbol_import" / "import_decomp_symbols.py"
SYMBOLS_DIR = ROOT / "symbols"
DEFAULT_ROM = ROOT / "roms" / "B8CE.gba"

def main():
    if not IMPORTER.exists():
        print(f"Error: Importer not found at {IMPORTER}")
        sys.exit(1)

    syms_file = SYMBOLS_DIR / "khcom_syms.txt"
    sections_file = SYMBOLS_DIR / "khcom_sections.txt"
    map_file = SYMBOLS_DIR / "khcom.map"
    rom_file = DEFAULT_ROM if DEFAULT_ROM.exists() else None

    if len(sys.argv) > 1:
        rom_file = Path(sys.argv[1])

    if not rom_file or not rom_file.exists():
        print("Notice: No existing ROM specified. Place B8CE.gba in roms/ or pass the path as an argument.")
        print("Usage: python tools/import_decomp_symbols.py [path_to_rom.gba]")
        sys.exit(1)

    cmd = [
        sys.executable,
        str(IMPORTER),
        "--id", "B8CE",
        "--name", "Kingdom Hearts: Chain of Memories (USA)",
        "--syms", str(syms_file),
        "--sections", str(sections_file),
        "--map", str(map_file),
        "--rom", str(rom_file),
        "--out", str(SYMBOLS_DIR)
    ]

    print(f"Running symbol import for {rom_file}...")
    res = subprocess.run(cmd)
    sys.exit(res.returncode)

if __name__ == "__main__":
    main()
