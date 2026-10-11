#include "runtime_ui_standard.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(GBARECOMP_RUNTIME_UI)

namespace khcom {

RecompRuntimeUi* g_active_runtime_ui = nullptr;

RecompRuntimeUi* create_standard_ui(const RecompRuntimeUiStandardConfig* standard, RuntimeUiCreateFn real) {
    if (!standard) return nullptr;
    RecompRuntimeUiStandardConfig cfg = *standard;
    cfg.features &= ~static_cast<uint64_t>(RECOMP_RUNTIME_UI_STANDARD_RESUME);
    cfg.view_modes = RECOMP_RUNTIME_UI_VIEW_MODE_NATIVE | RECOMP_RUNTIME_UI_VIEW_MODE_FIXED_16_9;
    cfg.native_view_label = "3:2 (Native)";
    cfg.fixed_view_label = "16:9 (Battles only)";
    g_active_runtime_ui = real(&cfg);
    const char* log = std::getenv("KHCOM_LOG_INTERPOSE");
    if (log && *log && std::strcmp(log, "0") != 0) {
        std::fprintf(stderr, "[khcom] recomp_runtime_ui_create_standard intercepted (menu=%p, view labels set, RESUME removed)\n",
                     static_cast<void*>(g_active_runtime_ui));
    }
    return g_active_runtime_ui;
}

} // namespace khcom

#if defined(KHCOM_RENAME_RUNTIME_UI_CREATE_STANDARD)
// MSVC: CMake compiles recomp-ui's recomp_runtime_settings.c with
// recomp_runtime_ui_create_standard renamed to khcom_real_recomp_runtime_ui_create_standard,
// so gbarecomp's call lands here and we forward to the renamed original.
extern "C" RecompRuntimeUi* khcom_real_recomp_runtime_ui_create_standard(const RecompRuntimeUiStandardConfig* standard);
extern "C" RecompRuntimeUi* recomp_runtime_ui_create_standard(const RecompRuntimeUiStandardConfig* standard) {
    return khcom::create_standard_ui(standard, &khcom_real_recomp_runtime_ui_create_standard);
}
#endif

#endif // GBARECOMP_RUNTIME_UI
