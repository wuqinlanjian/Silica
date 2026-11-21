#pragma once

namespace silica {

struct Color {
    float r = 0.f;
    float g = 0.f;
    float b = 0.f;
    float a = 1.f;

    constexpr Color() = default;
    constexpr Color(float r, float g, float b, float a = 1.f) : r(r), g(g), b(b), a(a) {}

    constexpr Color WithAlpha(float alpha) const { return {r, g, b, alpha}; }
    constexpr Color ScaleAlpha(float factor) const { return {r, g, b, a * factor}; }
};

inline constexpr Color kWhite{1.f, 1.f, 1.f, 1.f};
inline constexpr Color kBlack{0.f, 0.f, 0.f, 1.f};
inline constexpr Color kTransparent{0.f, 0.f, 0.f, 0.f};

} // namespace silica
