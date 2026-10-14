#include <string>
#include <vector>
#include "runtime.h"
#if defined(RECOMP_LAUNCHER)
#include "launcher_seam.h"
#endif
#define GBAIMP_HOST_MAIN_GBARECOMP
#include "gbaimp/host_main.h"
#include "game_config.h"
#include "ram_overlay_dispatch.h"
#include "capture_harness.h"

// sdl_intercepts.cpp: registers the SDL handlers (Linux: --wrap shims; Windows: IAT patching).
extern "C" bool khcom_install_sdl_interposers();

int main(int argc, char** argv) {
    gbaimp::HostMainConfig<gbarecomp::RunOptions> cfg;
    cfg.title = khcom::GAME_TITLE.data();
    cfg.banner_name = "KHCOMRecomp";
    // Must run before the runtime makes its first SDL call.
    cfg.install_sdl_interposers = &khcom_install_sdl_interposers;
    cfg.install_ram_dispatch = &khcom_install_ram_dispatch;
    // Headless harness (env-gated, no-op without KHCOM_* variables): savestate load/save, fast-forward.
    cfg.extra_args = [] { return khcom::capture::extra_runtime_args(); };
    cfg.create_options = [] { return khcom::create_run_options(); };
    return gbaimp::run_gbarecomp_host(cfg, argc, argv);
}
