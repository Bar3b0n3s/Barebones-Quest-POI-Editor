#pragma once

#include "wow/WorldMap.h"

#include <span>
#include <string>

namespace qpe
{
[[nodiscard]] RgbaImage DecodeBlp(std::span<std::uint8_t const> bytes, std::string& error);
}

