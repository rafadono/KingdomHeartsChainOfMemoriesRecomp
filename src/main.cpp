#include <cstdio>
#include <vector>
#include <string>
#include "runtime.h"
#include "game_config.h"
#include "ram_overlay_dispatch.h"
#include "capture_harness.h"

#if defined(RECOMP_LAUNCHER)
#include "launcher_seam.h"
#endif

// sdl_intercepts.cpp: registers the SDL handlers (Linux: --wrap shims; Windows: IAT patching).
extern "C" bool khcom_install_sdl_interposers();

int main(int argc, char** argv) {
    // Must run before the runtime makes its first SDL call.
    khcom_install_sdl_interposers();
    if (!std::getenv("GBARECOMP_RAM_OVERLAY_HEAL")) {
#if defined(_WIN32)
        _putenv("GBARECOMP_RAM_OVERLAY_HEAL=1");
#else
        setenv("GBARECOMP_RAM_OVERLAY_HEAL", "1", 1);
#endif
    }
    khcom_install_ram_dispatch();
    std::printf("%s Static Recompilation (KHCOMRecomp)\n", khcom::GAME_TITLE.data());
    // Headless harness (env-gated, no-op without KHCOM_* variables): savestate load/save, fast-forward.
    std::vector<std::string> harness_args = khcom::capture::extra_runtime_args();
    auto opts = khcom::create_run_options();

#if defined(RECOMP_LAUNCHER)
    std::vector<std::string> args(argv, argv + argc);
    args.insert(args.end(), harness_args.begin(), harness_args.end());
    if (gbarecomp_launcher_preboot(args, opts)) {
        return 0;
    }
    std::vector<char*> av;
    for (auto& s : args) {
        av.push_back(s.data());
    }
    return gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
#else
    if (harness_args.empty()) {
        return gbarecomp::run_game(argc, argv, opts);   // unchanged path when the harness is off
    }
    std::vector<std::string> args(argv, argv + argc);
    args.insert(args.end(), harness_args.begin(), harness_args.end());
    std::vector<char*> av;
    for (auto& s : args) {
        av.push_back(s.data());
    }
    return gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
#endif
}
