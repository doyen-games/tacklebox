// Draw-list effects and the icon set. Everything renders through ImDrawList
// paths: no textures, crisp at any DPI, one visual voice.
#pragma once

#include <imgui.h>

namespace tb::ui {

// --- easing / time ----------------------------------------------------------
float easeOutCubic(float t);
float pulse(float periodSec, float phase = 0.0f);  // 0..1 sine, honors reduceMotion

// --- glow primitives --------------------------------------------------------
// Layered translucent strokes around a rounded rect. intensity 0..1 scales
// with the user's glow preference.
void glowRect(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, float intensity,
              float rounding = 4.0f, float thickness = 1.0f);
void glowLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, float intensity,
              float thickness = 1.0f);
void glowCircle(ImDrawList* dl, ImVec2 center, float radius, ImU32 color, float intensity);

// Corner brackets: the Tron frame. len = bracket arm length.
void cornerBrackets(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, float len = 12.0f,
                    float thickness = 1.5f);

// Full-window backdrop: faint grid + radial falloff + drifting energy line.
void backdrop(ImDrawList* dl, ImVec2 min, ImVec2 max, double time);

// Thin animated sweep inside a rect (unlock screen flourish).
void scanline(ImDrawList* dl, ImVec2 min, ImVec2 max, double time, ImU32 color);

// --- icons ------------------------------------------------------------------
enum class Icon {
    Anchor,
    Send,
    Grid,        // contracts
    Shield,      // whitelist
    Key,         // vault
    Clock,       // history
    Gear,        // settings
    Lock,
    Unlock,
    Copy,
    Refresh,
    Warning,
    CheckCircle,
    XCircle,
    ChevronDown,
    ChevronUp,
    ChevronRight,
    Eye,
    EyeOff,
    Plus,
    Trash,
    External,
    Pin,
    Bolt,        // auto-sign
    Globe,       // network
    Pulse,       // dashboard
    QrCode,
};

// Stroke icon centered at `center` fitting a box of `size` px.
void drawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 color,
              float thickness = 1.8f);

// The anchor mark with glow, used on the unlock screen and sidebar.
// The brand mark: the tacklebox from assets/brand/tacklebox.svg, drawn as
// vector chrome inside the glowing ring (crisp at any DPI). Sizes under ~36
// drop the fine detail (seams, circuits, bobber) so small marks stay legible.
void drawTackleboxMark(ImDrawList* dl, ImVec2 center, float size, ImU32 color, float glow);

}  // namespace tb::ui
