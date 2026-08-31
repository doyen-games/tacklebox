#include "ui/fx.hpp"

#include <cmath>

#include "ui/theme.hpp"

namespace tb::ui {

float easeOutCubic(float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

float pulse(float periodSec, float phase) {
    if (cosmetics().reduceMotion) return 0.5f;
    double t = ImGui::GetTime();
    return 0.5f + 0.5f * std::sin((t / periodSec + phase) * 6.2831853f);
}

void glowRect(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, float intensity,
              float rounding, float thickness) {
    float g = intensity * cosmetics().glow;
    if (g > 0.01f) {
        for (int i = 1; i <= 3; ++i) {
            float expand = static_cast<float>(i) * 1.6f;
            float a = g * 0.16f / static_cast<float>(i);
            dl->AddRect({min.x - expand, min.y - expand}, {max.x + expand, max.y + expand},
                        col::alpha(color, a), rounding + expand, 2.0f);
        }
    }
    dl->AddRect(min, max, color, rounding, thickness);
}

void glowLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, float intensity,
              float thickness) {
    float g = intensity * cosmetics().glow;
    if (g > 0.01f) {
        dl->AddLine(a, b, col::alpha(color, g * 0.20f), thickness + 4.0f);
        dl->AddLine(a, b, col::alpha(color, g * 0.35f), thickness + 2.0f);
    }
    dl->AddLine(a, b, color, thickness);
}

void glowCircle(ImDrawList* dl, ImVec2 center, float radius, ImU32 color, float intensity) {
    float g = intensity * cosmetics().glow;
    if (g > 0.01f) {
        dl->AddCircle(center, radius + 3.0f, col::alpha(color, g * 0.18f), 0, 4.0f);
        dl->AddCircle(center, radius + 1.5f, col::alpha(color, g * 0.30f), 0, 2.0f);
    }
    dl->AddCircle(center, radius, color, 0, 1.6f);
}

void cornerBrackets(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, float len,
                    float thickness) {
    auto arm = [&](ImVec2 corner, float dx, float dy) {
        dl->AddLine(corner, {corner.x + dx * len, corner.y}, color, thickness);
        dl->AddLine(corner, {corner.x, corner.y + dy * len}, color, thickness);
    };
    arm(min, 1, 1);
    arm({max.x, min.y}, -1, 1);
    arm({min.x, max.y}, 1, -1);
    arm(max, -1, -1);
}

void backdrop(ImDrawList* dl, ImVec2 min, ImVec2 max, double time) {
    dl->AddRectFilled(min, max, col::Bg);

    // Fine grid, fading toward the top.
    const float step = 44.0f;
    float h = max.y - min.y;
    for (float x = min.x + std::fmod(step - std::fmod(min.x, step), step); x < max.x; x += step)
        dl->AddLine({x, min.y}, {x, max.y}, col::rgba(0x0F2438, 0x16), 1.0f);
    for (float y = min.y; y < max.y; y += step) {
        float depth = (y - min.y) / h;  // brighter near the floor
        unsigned a = static_cast<unsigned>(0x08 + depth * 0x1A);
        dl->AddLine({min.x, y}, {max.x, y}, col::rgba(0x0F2438, a), 1.0f);
    }

    // Radial vignette pulling focus to the middle.
    ImVec2 c{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.45f};
    dl->AddRectFilledMultiColor(min, {max.x, c.y}, col::rgba(0x04070C, 0xB0),
                                col::rgba(0x04070C, 0xB0), col::rgba(0x04070C, 0x00),
                                col::rgba(0x04070C, 0x00));
    dl->AddRectFilledMultiColor({min.x, c.y}, max, col::rgba(0x04070C, 0x00),
                                col::rgba(0x04070C, 0x00), col::rgba(0x04070C, 0x90),
                                col::rgba(0x04070C, 0x90));

    // A slow horizontal energy line drifting down the grid.
    if (!cosmetics().reduceMotion && cosmetics().glow > 0.01f) {
        float cycle = static_cast<float>(std::fmod(time * 0.045, 1.0));
        float y = min.y + cycle * h;
        float a = 0.10f * cosmetics().glow;
        dl->AddLine({min.x, y}, {max.x, y}, col::alpha(col::Cyan, a * 0.5f), 3.0f);
        dl->AddLine({min.x, y}, {max.x, y}, col::alpha(col::Cyan, a), 1.0f);
    }
}

void scanline(ImDrawList* dl, ImVec2 min, ImVec2 max, double time, ImU32 color) {
    if (cosmetics().reduceMotion) return;
    float h = max.y - min.y;
    float cycle = static_cast<float>(std::fmod(time * 0.35, 1.3)) - 0.15f;
    float y = min.y + cycle * h;
    if (y < min.y || y > max.y) return;
    float g = 0.16f * cosmetics().glow;
    dl->PushClipRect(min, max, true);
    dl->AddRectFilledMultiColor({min.x, y - 22.0f}, {max.x, y}, col::alpha(color, 0.0f),
                                col::alpha(color, 0.0f), col::alpha(color, g),
                                col::alpha(color, g));
    dl->AddLine({min.x, y}, {max.x, y}, col::alpha(color, g * 2.2f), 1.0f);
    dl->PopClipRect();
}

// --- icons ------------------------------------------------------------------

// Icon geometry works in a [-1, 1] unit box scaled by s = size/2.
void drawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float size, ImU32 color, float thickness) {
    const float s = size * 0.5f;
    auto P = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
    auto line = [&](float x1, float y1, float x2, float y2) {
        dl->AddLine(P(x1, y1), P(x2, y2), color, thickness);
    };

    switch (icon) {
        case Icon::Anchor:
            dl->AddCircle(P(0, -0.66f), s * 0.26f, color, 0, thickness);
            line(0, -0.4f, 0, 0.78f);
            line(-0.42f, -0.06f, 0.42f, -0.06f);
            dl->PathArcTo(P(0, 0.28f), s * 0.72f, 2.6f, 3.14159f + 3.14159f - 2.6f);
            dl->PathStroke(color, thickness);
            break;
        case Icon::Send:
            line(-0.7f, 0.7f, 0.7f, -0.7f);
            line(0.7f, -0.7f, 0.15f, -0.7f);
            line(0.7f, -0.7f, 0.7f, -0.15f);
            break;
        case Icon::Grid:
            dl->AddRect(P(-0.75f, -0.75f), P(-0.1f, -0.1f), color, 0, thickness);
            dl->AddRect(P(0.1f, -0.75f), P(0.75f, -0.1f), color, 0, thickness);
            dl->AddRect(P(-0.75f, 0.1f), P(-0.1f, 0.75f), color, 0, thickness);
            dl->AddRect(P(0.1f, 0.1f), P(0.75f, 0.75f), color, 0, thickness);
            break;
        case Icon::Shield:
            dl->PathLineTo(P(0, -0.8f));
            dl->PathLineTo(P(0.7f, -0.5f));
            dl->PathLineTo(P(0.7f, 0.1f));
            dl->PathLineTo(P(0, 0.8f));
            dl->PathLineTo(P(-0.7f, 0.1f));
            dl->PathLineTo(P(-0.7f, -0.5f));
            dl->PathLineTo(P(0, -0.8f));
            dl->PathStroke(color, thickness, ImDrawFlags_Closed);
            line(-0.28f, 0.0f, -0.06f, 0.26f);
            line(-0.06f, 0.26f, 0.34f, -0.22f);
            break;
        case Icon::Key:
            dl->AddCircle(P(-0.38f, 0.38f), s * 0.34f, color, 0, thickness);
            line(-0.14f, 0.14f, 0.72f, -0.72f);
            line(0.44f, -0.44f, 0.66f, -0.22f);
            line(0.72f, -0.72f, 0.72f, -0.42f);
            break;
        case Icon::Clock:
            dl->AddCircle(P(0, 0), s * 0.78f, color, 0, thickness);
            line(0, -0.42f, 0, 0.05f);
            line(0, 0.05f, 0.32f, 0.24f);
            break;
        case Icon::Gear: {
            dl->AddCircle(P(0, 0), s * 0.3f, color, 0, thickness);
            for (int i = 0; i < 8; ++i) {
                float ang = static_cast<float>(i) * 0.7854f;
                float ca = std::cos(ang), sa = std::sin(ang);
                dl->AddLine(P(ca * 0.52f, sa * 0.52f), P(ca * 0.8f, sa * 0.8f), color,
                            thickness);
            }
            break;
        }
        case Icon::Lock:
            dl->AddRect(P(-0.6f, -0.05f), P(0.6f, 0.8f), color, s * 0.1f, thickness);
            dl->PathArcTo(P(0, -0.1f), s * 0.38f, 3.14159f, 6.2831f);
            dl->PathStroke(color, thickness);
            dl->AddCircleFilled(P(0, 0.38f), s * 0.1f, color);
            break;
        case Icon::Unlock:
            dl->AddRect(P(-0.6f, -0.05f), P(0.6f, 0.8f), color, s * 0.1f, thickness);
            dl->PathArcTo(P(0.25f, -0.15f), s * 0.38f, 3.14159f, 4.9f);
            dl->PathStroke(color, thickness);
            break;
        case Icon::Copy:
            dl->AddRect(P(-0.7f, -0.7f), P(0.25f, 0.25f), color, 2, thickness);
            dl->AddRect(P(-0.25f, -0.25f), P(0.7f, 0.7f), color, 2, thickness);
            break;
        case Icon::Refresh:
            dl->PathArcTo(P(0, 0), s * 0.7f, -1.2f, 3.6f);
            dl->PathStroke(color, thickness);
            line(0.28f, -0.78f, 0.28f, -0.42f);
            line(0.28f, -0.42f, 0.64f, -0.5f);
            break;
        case Icon::Warning:
            dl->PathLineTo(P(0, -0.75f));
            dl->PathLineTo(P(0.8f, 0.65f));
            dl->PathLineTo(P(-0.8f, 0.65f));
            dl->PathStroke(color, thickness, ImDrawFlags_Closed);
            line(0, -0.3f, 0, 0.2f);
            dl->AddCircleFilled(P(0, 0.42f), thickness * 0.55f, color);
            break;
        case Icon::CheckCircle:
            dl->AddCircle(P(0, 0), s * 0.78f, color, 0, thickness);
            line(-0.34f, 0.02f, -0.08f, 0.3f);
            line(-0.08f, 0.3f, 0.4f, -0.26f);
            break;
        case Icon::XCircle:
            dl->AddCircle(P(0, 0), s * 0.78f, color, 0, thickness);
            line(-0.3f, -0.3f, 0.3f, 0.3f);
            line(-0.3f, 0.3f, 0.3f, -0.3f);
            break;
        case Icon::ChevronDown:
            line(-0.5f, -0.2f, 0, 0.3f);
            line(0, 0.3f, 0.5f, -0.2f);
            break;
        case Icon::ChevronUp:
            line(-0.5f, 0.2f, 0, -0.3f);
            line(0, -0.3f, 0.5f, 0.2f);
            break;
        case Icon::ChevronRight:
            line(-0.2f, -0.5f, 0.3f, 0);
            line(0.3f, 0, -0.2f, 0.5f);
            break;
        case Icon::Eye:
            dl->PathArcTo(P(0, 0.62f), s * 1.05f, -2.35f, -0.79f);
            dl->PathStroke(color, thickness);
            dl->PathArcTo(P(0, -0.62f), s * 1.05f, 0.79f, 2.35f);
            dl->PathStroke(color, thickness);
            dl->AddCircle(P(0, 0), s * 0.22f, color, 0, thickness);
            break;
        case Icon::EyeOff:
            dl->PathArcTo(P(0, 0.62f), s * 1.05f, -2.35f, -0.79f);
            dl->PathStroke(color, thickness);
            dl->PathArcTo(P(0, -0.62f), s * 1.05f, 0.79f, 2.35f);
            dl->PathStroke(color, thickness);
            line(-0.7f, 0.7f, 0.7f, -0.7f);
            break;
        case Icon::Plus:
            line(-0.6f, 0, 0.6f, 0);
            line(0, -0.6f, 0, 0.6f);
            break;
        case Icon::Trash:
            dl->AddRect(P(-0.52f, -0.4f), P(0.52f, 0.75f), color, 2, thickness);
            line(-0.7f, -0.4f, 0.7f, -0.4f);
            line(-0.22f, -0.4f, -0.22f, -0.62f);
            line(-0.22f, -0.62f, 0.22f, -0.62f);
            line(0.22f, -0.62f, 0.22f, -0.4f);
            line(-0.2f, -0.12f, -0.2f, 0.5f);
            line(0.2f, -0.12f, 0.2f, 0.5f);
            break;
        case Icon::External:
            dl->AddRect(P(-0.7f, -0.35f), P(0.35f, 0.7f), color, 2, thickness);
            line(0.0f, 0.0f, 0.7f, -0.7f);
            line(0.7f, -0.7f, 0.25f, -0.7f);
            line(0.7f, -0.7f, 0.7f, -0.25f);
            break;
        case Icon::Pin:
            dl->AddCircle(P(0, -0.25f), s * 0.42f, color, 0, thickness);
            line(0, 0.17f, 0, 0.8f);
            line(-0.3f, 0.45f, 0.3f, 0.45f);
            break;
        case Icon::Bolt:
            dl->PathLineTo(P(0.25f, -0.8f));
            dl->PathLineTo(P(-0.35f, 0.1f));
            dl->PathLineTo(P(0.02f, 0.1f));
            dl->PathLineTo(P(-0.25f, 0.8f));
            dl->PathLineTo(P(0.38f, -0.12f));
            dl->PathLineTo(P(0.0f, -0.12f));
            dl->PathStroke(color, thickness * 0.9f, ImDrawFlags_Closed);
            break;
        case Icon::Globe:
            dl->AddCircle(P(0, 0), s * 0.78f, color, 0, thickness);
            dl->AddEllipse(P(0, 0), {s * 0.34f, s * 0.78f}, color, 0, 0, thickness);
            line(-0.75f, 0, 0.75f, 0);
            break;
        case Icon::Pulse:
            line(-0.8f, 0.1f, -0.35f, 0.1f);
            line(-0.35f, 0.1f, -0.12f, -0.5f);
            line(-0.12f, -0.5f, 0.18f, 0.55f);
            line(0.18f, 0.55f, 0.38f, 0.1f);
            line(0.38f, 0.1f, 0.8f, 0.1f);
            break;
        case Icon::QrCode:
            dl->AddRect(P(-0.75f, -0.75f), P(-0.15f, -0.15f), color, 0, thickness);
            dl->AddRect(P(0.15f, -0.75f), P(0.75f, -0.15f), color, 0, thickness);
            dl->AddRect(P(-0.75f, 0.15f), P(-0.15f, 0.75f), color, 0, thickness);
            dl->AddRectFilled(P(0.15f, 0.15f), P(0.4f, 0.4f), color);
            dl->AddRectFilled(P(0.5f, 0.5f), P(0.75f, 0.75f), color);
            dl->AddRectFilled(P(0.5f, 0.15f), P(0.75f, 0.28f), color);
            break;
    }
}

void drawAnchorMark(ImDrawList* dl, ImVec2 center, float size, ImU32 color, float glow) {
    float g = glow * cosmetics().glow;
    if (g > 0.01f) {
        for (int i = 3; i >= 1; --i)
            dl->AddCircle(center, size * (0.62f + 0.05f * static_cast<float>(i)),
                          col::alpha(color, g * 0.10f), 0, 3.0f);
    }
    dl->AddCircle(center, size * 0.62f, col::alpha(color, 0.85f), 0, 1.6f);
    drawIcon(dl, Icon::Anchor, center, size * 0.72f, color, 2.2f);
}

}  // namespace tb::ui
