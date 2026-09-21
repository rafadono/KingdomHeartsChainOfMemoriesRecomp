#pragma once

#include <cstdint>
#include <string_view>
#include "runtime.h"

namespace khcom {

inline constexpr std::string_view GAME_TITLE = "Kingdom Hearts: Chain of Memories";
inline constexpr std::string_view GAME_ID = "B8CE";
inline constexpr std::string_view ROM_SHA1_USA = "10729bd884f8fdca7a310b6d606c52e46657aa48";
inline constexpr std::size_t ROM_SIZE_USA = 33554432;

gbarecomp::RunOptions create_run_options();

}
