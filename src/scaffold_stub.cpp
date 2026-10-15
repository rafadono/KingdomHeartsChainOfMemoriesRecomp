#include "gbaimp/scaffold_stub.h"

GBAIMP_SCAFFOLD_EMPTY_DISPATCH_TABLE()

// The unit-test target links the real game_config.cpp (KHCOM_TEST_REAL_CONFIG) and only needs the dispatch table.
#if !defined(KHCOM_TEST_REAL_CONFIG)
namespace khcom {
bool is_runtime_menu_open() {
    return false;
}
GBAIMP_WEAK void save_khcom_config() {}
GBAIMP_WEAK void load_khcom_config() {}
}
#endif
