#include "gbaimp/scaffold_stub.h"

GBAIMP_SCAFFOLD_EMPTY_DISPATCH_TABLE()

namespace khcom {
bool is_runtime_menu_open() {
    return false;
}
GBAIMP_WEAK void save_khcom_config() {}
GBAIMP_WEAK void load_khcom_config() {}
}
