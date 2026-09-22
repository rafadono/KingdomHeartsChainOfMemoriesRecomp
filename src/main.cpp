#include <cstdio>
#include <vector>
#include <string>
#include "runtime.h"
#include "game_config.h"
#include "ram_overlay_dispatch.h"

#if defined(RECOMP_LAUNCHER)
#include "launcher_seam.h"
#endif

int main(int argc, char** argv) {
    if (!std::getenv("GBARECOMP_RAM_OVERLAY_HEAL")) {
#if defined(_WIN32)
        _putenv("GBARECOMP_RAM_OVERLAY_HEAL=1");
#else
        setenv("GBARECOMP_RAM_OVERLAY_HEAL", "1", 1);
#endif
    }
    khcom_install_ram_dispatch();
    std::printf("%s Static Recompilation (KHCOMRecomp)\n", khcom::GAME_TITLE.data());
    auto opts = khcom::create_run_options();

#if defined(RECOMP_LAUNCHER)
    std::vector<std::string> args(argv, argv + argc);
    if (gbarecomp_launcher_preboot(args, opts)) {
        return 0;
    }
    std::vector<char*> av;
    for (auto& s : args) {
        av.push_back(s.data());
    }
    return gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
#else
    return gbarecomp::run_game(argc, argv, opts);
#endif
}
