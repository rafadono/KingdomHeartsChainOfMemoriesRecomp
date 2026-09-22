import os
import glob
import re

def is_ram_pc(pc):
    return (0x02000000 <= pc < 0x02040000) or (0x03000000 <= pc < 0x03008000)

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cache_glob = os.path.join(root, "recomp_cache", "**", "abi5-ram3", "*.c")
    c_files = glob.glob(cache_glob, recursive=True)
    
    print(f"Scanning {len(c_files)} total .c files in cache...")

    entries = []
    seen_keys = set()
    
    for path in c_files:
        filename = os.path.basename(path)
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            content = f.read()

        m = re.search(r"// function 0x([0-9A-Fa-f]+) mode=(arm|thumb) end=0x([0-9A-Fa-f]+)", content)
        if not m:
            continue

        pc_str, mode_str, end_str = m.groups()
        pc = int(pc_str, 16)
        
        # Only process RAM (IWRAM and EWRAM) routines
        if not is_ram_pc(pc):
            continue

        thumb = 1 if mode_str == "thumb" else 0
        key = (pc, thumb)
        if key in seen_keys:
            continue
        seen_keys.add(key)

        end_pc = int(end_str, 16)

        fn_start = content.find(f"func_{pc_str}")
        if fn_start == -1:
            fn_start = content.find(f"func_{pc:08X}")
        if fn_start == -1:
            print(f"Warning: could not find func_{pc:08X} in {filename}")
            continue

        decl_start = content.rfind("OVL_EXPORT void", 0, fn_start)
        if decl_start == -1:
            decl_start = content.rfind("void", 0, fn_start)

        fn_code = content[decl_start:]
        # Normalize OVL_EXPORT void func_XXXXX to static void ram_func_XXXXX
        fn_code = re.sub(r"OVL_EXPORT\s+void\s+func_([0-9A-Fa-f]+)", r"static void ram_func_\1", fn_code, count=1)
        
        entries.append({
            "pc": pc,
            "thumb": thumb,
            "end": end_pc,
            "fn_name": f"ram_func_{pc:08X}",
            "code": fn_code.strip()
        })

    # Sort entries by PC then thumb mode for binary search
    entries.sort(key=lambda x: (x["pc"], x["thumb"]))
    print(f"Extracted {len(entries)} unique RAM routines (IWRAM/EWRAM).")

    out_header_path = os.path.join(root, "src", "ram_overlay_dispatch.h")
    header_content = """#pragma once
#include <cstdint>

extern "C" int khcom_ram_dispatch_hook(uint32_t pc, int thumb);
void khcom_install_ram_dispatch();
"""
    with open(out_header_path, "w", encoding="utf-8") as f:
        f.write(header_content)
    print(f"Wrote {out_header_path}")

    out_cpp_path = os.path.join(root, "src", "ram_overlay_dispatch.cpp")
    with open(out_cpp_path, "w", encoding="utf-8") as f:
        f.write("""// AUTO-GENERATED RAM overlay native dispatcher for Kingdom Hearts: Chain of Memories
// Directly compiles and dispatches all 168 IWRAM/EWRAM battle routines natively.

#include "ram_overlay_dispatch.h"
#include "runtime_arm.h"
#include <algorithm>
#include <cstdio>

namespace {

""")

        for e in entries:
            f.write(f"// --- Function 0x{e['pc']:08X} ({'thumb' if e['thumb'] else 'arm'}) ---\n")
            f.write(e["code"])
            f.write("\n\n")

        f.write("""struct RamDispatchEntry {
    uint32_t pc;
    int thumb;
    void (*fn)(void);
};

static const RamDispatchEntry kRamDispatchEntries[] = {
""")
        for e in entries:
            f.write(f"    {{ 0x{e['pc']:08X}u, {e['thumb']}, {e['fn_name']} }},\n")

        f.write("""};

constexpr size_t kRamDispatchCount = sizeof(kRamDispatchEntries) / sizeof(kRamDispatchEntries[0]);

} // namespace

extern "C" int khcom_ram_dispatch_hook(uint32_t pc, int thumb) {
    uint32_t target_pc = pc & ~1u;
    int lo = 0;
    int hi = static_cast<int>(kRamDispatchCount) - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        const auto& e = kRamDispatchEntries[mid];
        if (e.pc < target_pc) {
            lo = mid + 1;
        } else if (e.pc > target_pc) {
            hi = mid - 1;
        } else {
            if (e.thumb == thumb) {
                e.fn();
                return 1;
            }
            if (mid > 0 && kRamDispatchEntries[mid - 1].pc == target_pc && kRamDispatchEntries[mid - 1].thumb == thumb) {
                kRamDispatchEntries[mid - 1].fn();
                return 1;
            }
            if (mid + 1 < static_cast<int>(kRamDispatchCount) && kRamDispatchEntries[mid + 1].pc == target_pc && kRamDispatchEntries[mid + 1].thumb == thumb) {
                kRamDispatchEntries[mid + 1].fn();
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

void khcom_install_ram_dispatch() {
    static bool s_installed = false;
    if (!s_installed) {
        g_runtime_ram_dispatch_hook = khcom_ram_dispatch_hook;
        s_installed = true;
        std::printf("[KHCOMRecomp] Native RAM overlay dispatcher installed (%zu functions registered)\\n", kRamDispatchCount);
    }
}
""")
    print(f"Wrote {out_cpp_path} successfully ({len(entries)} functions).")

if __name__ == "__main__":
    main()
