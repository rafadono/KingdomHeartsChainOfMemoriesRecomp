#pragma once
// Game-specific tuning of the runtime settings menu built by recomp-ui's
// recomp_runtime_ui_create_standard() (called from gbarecomp's run_game()).
// Neither gbarecomp nor recomp-ui offers a host-side hook for the view-mode labels
// or the RESUME row, so the call is interposed: `--wrap` with GNU ld, a
// compile-time symbol rename of the recomp-ui translation unit with MSVC (see
// CMakeLists.txt and docs/GBA_IMPROVEMENTS_INTEGRATION.md). Both paths run the same
// create_standard_ui() below, so the resulting menu is identical.
#if defined(GBARECOMP_RUNTIME_UI)
#include "recomp_runtime_ui.h"

namespace khcom {

// The menu created by run_game(), used by the open/close/toggle helpers in game_config.cpp.
extern RecompRuntimeUi* g_active_runtime_ui;

using RuntimeUiCreateFn = RecompRuntimeUi* (*)(const RecompRuntimeUiStandardConfig*);

// Applies the KH view-mode labels / feature set and forwards to `real`.
RecompRuntimeUi* create_standard_ui(const RecompRuntimeUiStandardConfig* standard, RuntimeUiCreateFn real);

} // namespace khcom
#endif
