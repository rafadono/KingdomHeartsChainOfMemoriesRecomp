#include "runtime_ui_standard.h"

#if defined(GBARECOMP_RUNTIME_UI)

namespace khcom {

RecompRuntimeUi* g_active_runtime_ui = nullptr;

const gbaimp::RuntimeUiGlueOptions& runtime_ui_glue_options() {
    static const gbaimp::RuntimeUiGlueOptions opt = [] {
        gbaimp::RuntimeUiGlueOptions o;
        o.tag = "khcom";
        o.log_env = "KHCOM_LOG_INTERPOSE";
        o.native_view_label = "3:2 (Native)";
        o.fixed_view_label = "16:9 (Battles only)";
        return o;
    }();
    return opt;
}

} // namespace khcom

#if defined(KHCOM_RENAME_RUNTIME_UI_CREATE_STANDARD)
// MSVC: CMake compiles recomp-ui's recomp_runtime_settings.c with
// recomp_runtime_ui_create_standard renamed to khcom_real_recomp_runtime_ui_create_standard,
// so gbarecomp's call lands here and we forward to the renamed original.
GBAIMP_RUNTIME_UI_RENAME_FORWARDER(khcom_real_recomp_runtime_ui_create_standard,
                                   khcom::runtime_ui_glue_options(), &khcom::g_active_runtime_ui)
#endif

// GNU ld: `-Wl,--wrap=recomp_runtime_ui_create_standard` (CMakeLists.txt).
GBAIMP_RUNTIME_UI_WRAP(khcom::runtime_ui_glue_options(), &khcom::g_active_runtime_ui)

#endif // GBARECOMP_RUNTIME_UI
