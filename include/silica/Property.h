#pragma once

#include "Color.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace silica::property {

std::string_view Trim(std::string_view text);
std::string      ToLower(std::string_view text);

// #RGB, #RGBA, #RRGGBB and #RRGGBBAA (alpha last, CSS style), plus the basic
// color names and "transparent"/"none".
std::optional<Color> ParseColor(std::string_view text);

// Accepts an optional px/em/pt/dp suffix and rejects trailing garbage.
std::optional<float> ParseNumber(std::string_view text);

std::optional<bool> ParseBool(std::string_view text);

std::vector<std::string> Split(std::string_view text);

struct BorderSpec {
    std::optional<float> width;
    std::optional<Color> color;
};

// "1", "2 #4CAF50", "#4CAF50 solid", "none"
std::optional<BorderSpec> ParseBorder(std::string_view text);

} // namespace silica::property
