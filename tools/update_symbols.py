import os

def update_symbols():
    symbols = {}

    # Initial core entries
    symbols[0x08000000] = ('arm', 'start_vector')
    symbols[0x080000C0] = ('arm', 'crt0_entry')

    # Runtime discovered seeds
    symbols[0x080D7014] = ('thumb', 'sound_driver_init')
    symbols[0x080D7160] = ('thumb', 'sound_driver_step')
    symbols[0x080D71D4] = ('thumb', 'sound_driver_sub')
    symbols[0x0811F578] = ('thumb', 'intr_manager_step')

    # Load from task_mode_symbols.txt
    task_syms_path = "symbols/task_mode_symbols.txt"
    if os.path.exists(task_syms_path):
        with open(task_syms_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split()
                if len(parts) >= 3 and parts[0] == "thumb_func":
                    addr = int(parts[1], 16)
                    name = parts[2]
                    symbols[addr] = ('thumb', name)

    # Write out symbols/imported_symbols.tsv
    with open("symbols/imported_symbols.tsv", "w", encoding="utf-8") as f:
        for addr in sorted(symbols.keys()):
            mode, name = symbols[addr]
            f.write(f"0x{addr:08X}\t{mode}\t{name}\n")

    print(f"Wrote {len(symbols)} symbols to symbols/imported_symbols.tsv")

if __name__ == "__main__":
    update_symbols()
