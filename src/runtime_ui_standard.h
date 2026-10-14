#pragma once
// Game-specific tuning of the runtime settings menu built by recomp-ui's
// recomp_runtime_ui_create_standard() (called from gbarecomp's run_game()).
// The interposition and the menu adjustment are generic and live in the gba-improvements
// library (gbaimp/runtime_ui_glue.h); this host only provides its labels and keeps the menu
// pointer. See docs/GBA_IMPROVEMENTS_INTEGRATION.md.
#if defined(GBARECOMP_RUNTIME_UI)
#include "gbaimp/runtime_ui_glue.h"

namespace khcom {

// The menu created by run_game(), used by the open/close/toggle helpers in game_config.cpp.
extern RecompRuntimeUi* g_active_runtime_ui;

// View-mode labels and diagnostics settings of this game's menu.
const gbaimp::RuntimeUiGlueOptions& runtime_ui_glue_options();

} // namespace khcom
#endif
