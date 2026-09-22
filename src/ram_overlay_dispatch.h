#pragma once
#include <cstdint>

extern "C" int khcom_ram_dispatch_hook(uint32_t pc, int thumb);
void khcom_install_ram_dispatch();
