#include <cstdint>

struct DispatchEntry {
    uint32_t addr;
    uint8_t thumb;
    uint8_t resume;
    void (*fn)(void);
};

extern "C" const DispatchEntry kDispatchTable[1] = {{0, 0, 0, nullptr}};
extern "C" const unsigned kDispatchTableLen = 0;
