import glob
import os
import re
import sys

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    b8ce_toml_path = os.path.join(root, "config", "b8ce.toml")
    symbols_tsv_path = os.path.join(root, "symbols", "imported_symbols.tsv")
    task_symbols_path = os.path.join(root, "symbols", "task_mode_symbols.txt")
    cfg_symbols_path = os.path.join(root, "symbols", "reachable_cfg_symbols.tsv")
    cache_dir = os.path.join(root, "recomp_cache")
    rom_path = os.path.join(root, "roms", "Kingdom Hearts - Chain of Memories (U)(Venom).gba")

    target_count = 3650
    for arg in sys.argv[1:]:
        if arg.startswith("--target="):
            target_count = int(arg.split("=")[1])

    with open(b8ce_toml_path, "r", encoding="utf-8", errors="ignore") as f:
        orig_content = f.read()

    header_end = orig_content.find("[[extra_func]]")
    if header_end != -1:
        header = orig_content[:header_end].strip()
    else:
        header = orig_content.strip()

    funcs = {}

    # 1. Collect existing ROM extra_func
    for m in re.finditer(r'\[\[extra_func\]\]\s*addr\s*=\s*(0x[0-9a-fA-F]+)\s*mode\s*=\s*\"([^\"]+)\"(?:\s*name\s*=\s*\"([^\"]+)\")?', orig_content):
        addr = int(m.group(1), 16)
        mode = m.group(2)
        name = m.group(3)
        if addr >= 0x08000000:
            funcs[addr] = (mode, name)

    initial_count = len(funcs)

    # 2. Collect from imported_symbols.tsv
    tsv_count = 0
    if os.path.exists(symbols_tsv_path):
        with open(symbols_tsv_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                parts = line.split("\t")
                addr = int(parts[0], 16)
                if addr >= 0x08000000:
                    mode = parts[1]
                    name = parts[2] if len(parts) > 2 else None
                    if addr not in funcs:
                        funcs[addr] = (mode, name)
                        tsv_count += 1
                    elif not funcs[addr][1] and name:
                        funcs[addr] = (mode, name)

    # 3. Collect from task_mode_symbols.txt
    task_count = 0
    if os.path.exists(task_symbols_path):
        with open(task_symbols_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split()
                if len(parts) >= 3:
                    addr = int(parts[1], 16)
                    mode = "thumb" if parts[0] == "thumb_func" else "arm"
                    name = parts[2]
                    if addr >= 0x08000000:
                        if addr not in funcs:
                            funcs[addr] = (mode, name)
                            task_count += 1
                        elif not funcs[addr][1] and name:
                            funcs[addr] = (mode, name)

    # 4. Collect ROM functions from recomp_cache
    cache_count = 0
    for dll in glob.glob(os.path.join(cache_dir, "**", "*.dll"), recursive=True):
        fname = os.path.basename(dll)
        if fname.startswith("08"):
            parts = fname[:-4].split("_")
            pc = int(parts[0], 16)
            mode = "thumb" if parts[2] == "t" else "arm"
            if pc not in funcs:
                funcs[pc] = (mode, None)
                cache_count += 1

    # 5. Collect from reachable_cfg_symbols.tsv up to target_count
    cfg_count = 0
    if os.path.exists(cfg_symbols_path) and len(funcs) < target_count:
        with open(cfg_symbols_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                parts = line.split("\t")
                addr = int(parts[0], 16)
                mode = parts[1]
                name = parts[2] if len(parts) > 2 else None
                if addr >= 0x08000000 and addr not in funcs:
                    funcs[addr] = (mode, name)
                    cfg_count += 1
                    if len(funcs) >= target_count:
                        break

    # 6. Ingest validated prologue candidates from ROM if still under target_count
    prologue_count = 0
    if os.path.exists(rom_path) and len(funcs) < target_count:
        with open(rom_path, "rb") as f:
            rom = f.read()
        rom_base = 0x08000000
        for off in range(0, min(len(rom) - 4, 0x140000), 2):
            if len(funcs) >= target_count:
                break
            addr = rom_base + off
            if addr in funcs:
                continue
            b0, b1 = rom[off], rom[off + 1]
            if b1 == 0xB5: # PUSH {..., lr}
                # Check for plausible boundary
                if off >= 2:
                    p0, p1 = rom[off - 2], rom[off - 1]
                    if p1 == 0xBD or (p1 == 0x47 and p0 == 0x70) or (p0 == 0 and p1 == 0):
                        funcs[addr] = ("thumb", f"func_{addr:08X}")
                        prologue_count += 1

    sorted_pcs = sorted(funcs.keys())
    print(f"Functions in original TOML: {initial_count}")
    print(f"Added from symbols TSV: {tsv_count}")
    print(f"Added from task symbols: {task_count}")
    print(f"Added from recomp cache: {cache_count}")
    print(f"Added from reachable CFG: {cfg_count}")
    print(f"Added from validated prologues: {prologue_count}")
    print(f"Total ROM functions to recompile: {len(sorted_pcs)}")

    out_lines = [header, ""]
    for pc in sorted_pcs:
        mode, name = funcs[pc]
        out_lines.append("[[extra_func]]")
        out_lines.append(f"addr = 0x{pc:08X}")
        out_lines.append(f'mode = "{mode}"')
        if name:
            out_lines.append(f'name = "{name}"')
        out_lines.append("")

    with open(b8ce_toml_path, "w", encoding="utf-8") as f:
        f.write("\n".join(out_lines))

    print(f"Updated {b8ce_toml_path} successfully.")

if __name__ == "__main__":
    main()
