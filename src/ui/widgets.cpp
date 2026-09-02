#include "ui/widgets.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <functional>

#include <qrcodegen.hpp>

#include "core/util.hpp"
#include "guard/rules.hpp"
#include "ui/layout.hpp"
#include "ui/ui_helpers.h"

namespace tb::ui {

// --- typography -------------------------------------------------------------

void heading(const char* text, float size) {
    ImGui::PushFont(fonts().uiSemi, size);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void subtext(const char* text) {
    ImGui::PushFont(fonts().ui, kTextSm);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void sectionTitle(const char* text) {
    // Spaced capitals, then a hairline running to the right edge.
    std::string spaced;
    for (const char* p = text; *p; ++p) {
        spaced += static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
        if (*(p + 1)) spaced += ' ';
    }
    ImGui::PushFont(fonts().uiSemi, kTextSm);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
    ImGui::TextUnformatted(spaced.c_str());
    ImGui::PopStyleColor();
    ImVec2 textMax = ImGui::GetItemRectMax();
    ImVec2 textMin = ImGui::GetItemRectMin();
    ImGui::PopFont();
    float lineY = (textMin.y + textMax.y) * 0.5f;
    float availRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (availRight - textMax.x > 20.0f)
        ImGui::GetWindowDrawList()->AddLine({textMax.x + 12.0f, lineY}, {availRight, lineY},
                                            col::Hairline, 1.0f);
    ImGui::Dummy({0, 2});
}

void monoText(const std::string& text, ImU32 color, float size) {
    ImGui::PushFont(fonts().mono, size);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(color));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void vspace(float px) { ImGui::Dummy({0, px}); }

void assetText(const std::string& asset, float size, bool dim) {
    // "12.3456 WAX" splits at the last space when the tail reads as a symbol
    // code (1-7 uppercase letters); anything else renders whole.
    size_t space = asset.rfind(' ');
    bool split = space != std::string::npos && space + 1 < asset.size() &&
                 asset.size() - space - 1 <= 7;
    if (split)
        for (size_t i = space + 1; i < asset.size(); ++i)
            if (asset[i] < 'A' || asset[i] > 'Z') {
                split = false;
                break;
            }
    ImU32 amount = dim ? col::alpha(col::Amount, 0.62f) : col::Amount;
    ImU32 ticker = dim ? col::alpha(col::Ticker, 0.62f) : col::Ticker;
    ImGui::PushFont(fonts().mono, size);
    if (!split) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(amount));
        ImGui::TextUnformatted(asset.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(amount));
        ImGui::TextUnformatted(asset.c_str(), asset.c_str() + space);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, ImGui::CalcTextSize(" ").x);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(ticker));
        ImGui::TextUnformatted(asset.c_str() + space + 1);
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();
}

// --- containers -------------------------------------------------------------

bool beginCard(const char* id, float width, bool brackets) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col::vec(col::Panel));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
    bool open = ImGui::BeginChild(id, {width, 0},
                                  ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                                      ImGuiChildFlags_AlwaysUseWindowPadding);
    if (open && brackets) {
        ImVec2 min = ImGui::GetWindowPos();
        ImVec2 max = {min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y};
        cornerBrackets(ImGui::GetWindowDrawList(), {min.x + 4, min.y + 4},
                       {max.x - 4, max.y - 4}, col::alpha(col::Cyan, 0.5f), 10.0f, 1.2f);
    }
    (void)style;
    return open;
}

void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// --- controls ---------------------------------------------------------------

bool neonButton(const char* label, BtnKind kind, ImVec2 size, bool disabled) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushFont(fonts().uiSemi, kText);

    ImVec2 textSize = ImGui::CalcTextSize(label);
    ImVec2 pad{22, 10};
    // Auto-sized buttons honor the canon floors: 120*scale wide,
    // GetFrameHeight()*1.4 tall. -FLT_MIN fills the available width
    // (equal-stretch inside beginButtonRow tables).
    ImVec2 box{size.x > 0 ? size.x
               : size.x == -FLT_MIN
                   ? ImGui::GetContentRegionAvail().x
                   : std::max(textSize.x + pad.x * 2, 120.0f * dpiScale()),
               size.y > 0 ? size.y
                          : std::max(textSize.y + pad.y * 2,
                                     ImGui::GetFrameHeight() * 1.4f)};

    if (disabled) ImGui::BeginDisabled();
    bool pressed = ImGui::InvisibleButton(label, box);
    if (disabled) ImGui::EndDisabled();
    if (!disabled) ::ui::HandOnHover();

    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();

    ImU32 accent = kind == BtnKind::Danger ? col::Danger : col::Cyan;
    float dim = disabled ? 0.35f : 1.0f;

    switch (kind) {
        case BtnKind::Primary: {
            ImU32 fill = col::alpha(accent, (held ? 0.30f : hovered ? 0.22f : 0.14f) * dim);
            dl->AddRectFilled(min, max, fill, 3.0f);
            glowRect(dl, min, max, col::alpha(accent, 0.9f * dim),
                     hovered && !disabled ? 0.9f : 0.45f, 3.0f, 1.2f);
            break;
        }
        case BtnKind::Danger: {
            ImU32 fill = col::alpha(accent, (held ? 0.30f : hovered ? 0.20f : 0.10f) * dim);
            dl->AddRectFilled(min, max, fill, 3.0f);
            glowRect(dl, min, max, col::alpha(accent, 0.9f * dim),
                     hovered && !disabled ? 0.8f : 0.3f, 3.0f, 1.2f);
            break;
        }
        case BtnKind::Ghost:
            dl->AddRectFilled(min, max,
                              col::alpha(col::HairHi, held ? 0.35f : hovered ? 0.22f : 0.0f),
                              3.0f);
            dl->AddRect(min, max, col::alpha(col::HairHi, dim), 3.0f, 1.0f);
            break;
        case BtnKind::Subtle:
            if (hovered || held)
                dl->AddRectFilled(min, max, col::alpha(col::HairHi, held ? 0.30f : 0.18f), 3.0f);
            break;
    }

    ImU32 textCol = kind == BtnKind::Primary   ? col::alpha(col::Ice, dim)
                    : kind == BtnKind::Danger  ? col::alpha(col::rgba(0xFFD9DF), dim)
                    : hovered && !disabled     ? col::Ice
                                               : col::alpha(col::Steel, dim);
    dl->AddText({min.x + (box.x - textSize.x) * 0.5f, min.y + (box.y - textSize.y) * 0.5f},
                textCol, label);
    ImGui::PopFont();
    return pressed && !disabled;
}

bool iconButton(const char* id, Icon icon, const char* tip, ImU32 color, float size) {
    float box = size + 12.0f;
    bool pressed = ImGui::InvisibleButton(id, {box, box});
    ::ui::HandOnHover();
    ImVec2 min = ImGui::GetItemRectMin();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool hovered = ImGui::IsItemHovered();
    if (hovered)
        dl->AddRectFilled(min, ImGui::GetItemRectMax(), col::alpha(col::HairHi, 0.25f), 4.0f);
    drawIcon(dl, icon, {min.x + box * 0.5f, min.y + box * 0.5f}, size,
             hovered ? col::Ice : color, 1.7f);
    if (hovered && tip && *tip) tooltip(tip);
    return pressed;
}

bool holdButton(const char* label, float holdSec, float* progress, ImVec2 size, ImU32 color) {
    ImGui::PushFont(fonts().uiSemi, kText);
    ImVec2 textSize = ImGui::CalcTextSize(label);
    ImVec2 box{size.x > 0 ? size.x : textSize.x + 44, size.y > 0 ? size.y : textSize.y + 20};
    ImGui::InvisibleButton(label, box);
    ::ui::HandOnHover();
    bool held = ImGui::IsItemActive();
    ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    bool fired = false;
    if (held) {
        *progress += ImGui::GetIO().DeltaTime / holdSec;
        if (*progress >= 1.0f) {
            *progress = 0.0f;
            fired = true;
        }
    } else {
        *progress = std::max(0.0f, *progress - ImGui::GetIO().DeltaTime * 2.5f);
    }

    dl->AddRectFilled(min, max, col::alpha(color, 0.10f), 3.0f);
    if (*progress > 0.0f) {
        float w = (max.x - min.x) * easeOutCubic(*progress);
        dl->PushClipRect(min, {min.x + w, max.y}, true);
        dl->AddRectFilled(min, max, col::alpha(color, 0.45f), 3.0f);
        dl->PopClipRect();
    }
    glowRect(dl, min, max, color, held ? 1.0f : 0.35f, 3.0f, 1.2f);
    dl->AddText({min.x + (box.x - textSize.x) * 0.5f, min.y + (box.y - textSize.y) * 0.5f},
                col::rgba(0xFFE3E8), label);
    ImGui::PopFont();
    if (ImGui::IsItemHovered() && !held) tooltip("Press and hold");
    return fired;
}

bool textField(const char* label, char* buf, size_t bufSize, const FieldOpts& opts) {
    ImGui::PushID(label);
    // "##name" labels are IDs only, never rendered.
    if (label && *label && !(label[0] == '#' && label[1] == '#')) {
        ImGui::PushFont(fonts().uiSemi, kTextSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy({0, 1});
    }

    float width = opts.width > 0 ? opts.width : ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(width);

    ImGuiInputTextFlags flags = opts.flags | ImGuiInputTextFlags_EnterReturnsTrue;
    if (opts.password) flags |= ImGuiInputTextFlags_Password;

    if (opts.mono || opts.password) ImGui::PushFont(fonts().mono, kMono);
    if (opts.autoFocus && !ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0))
        ImGui::SetKeyboardFocusHere();
    bool entered = ImGui::InputTextWithHint("##field", opts.placeholder ? opts.placeholder : "",
                                            buf, bufSize, flags);
    if (opts.mono || opts.password) ImGui::PopFont();

    // Focus/error accent on the frame.
    ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (opts.error && *opts.error)
        glowRect(dl, min, max, col::alpha(col::Danger, 0.8f), 0.4f, 3.0f, 1.0f);
    else if (ImGui::IsItemActive())
        glowRect(dl, min, max, col::alpha(col::Cyan, 0.7f), 0.5f, 3.0f, 1.0f);

    if (opts.error && *opts.error) {
        ImGui::PushFont(fonts().ui, kTextSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Danger));
        ImGui::TextWrapped("%s", opts.error);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    } else if (opts.hint && *opts.hint) {
        ImGui::PushFont(fonts().ui, kTextSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
        ImGui::TextWrapped("%s", opts.hint);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::PopID();
    return entered;
}

bool toggle(const char* label, bool* value, const char* sub) {
    ImGui::PushID(label);
    const float trackW = 40.0f, trackH = 20.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec2 start = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##t", {trackW, trackH});
    ::ui::HandOnHover();
    if (clicked) *value = !*value;
    bool hovered = ImGui::IsItemHovered();

    // Animate the knob.
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImGuiID key = ImGui::GetID("anim");
    float anim = storage->GetFloat(key, *value ? 1.0f : 0.0f);
    float target = *value ? 1.0f : 0.0f;
    float dt = cosmetics().reduceMotion ? 1.0f : ImGui::GetIO().DeltaTime * 10.0f;
    anim += (target - anim) * std::min(1.0f, dt);
    storage->SetFloat(key, anim);

    ImVec2 min = start, max = {start.x + trackW, start.y + trackH};
    ImU32 track = *value ? col::alpha(col::Cyan, 0.25f) : col::rgba(0x0B1522);
    dl->AddRectFilled(min, max, track, trackH * 0.5f);
    dl->AddRect(min, max, *value ? col::alpha(col::Cyan, 0.8f) : col::Hairline, trackH * 0.5f,
                1.0f);
    float knobX = min.x + trackH * 0.5f + anim * (trackW - trackH);
    ImU32 knob = *value ? col::Cyan : col::Steel;
    if (*value && (hovered || anim < 0.99f))
        glowCircle(dl, {knobX, min.y + trackH * 0.5f}, trackH * 0.32f, knob, 0.6f);
    else
        dl->AddCircleFilled({knobX, min.y + trackH * 0.5f}, trackH * 0.32f, knob);

    ImGui::SameLine(0, 12);
    ImGui::BeginGroup();
    ImGui::PushFont(fonts().uiMedium, kText);
    ImGui::TextUnformatted(label);
    ImGui::PopFont();
    if (sub && *sub) {
        ImGui::PushFont(fonts().ui, kTextSm);
        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
        ImGui::TextWrapped("%s", sub);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::EndGroup();
    if (ImGui::IsItemClicked()) {
        *value = !*value;
        clicked = true;
    }
    ImGui::PopID();
    return clicked;
}

// --- indicators -------------------------------------------------------------

static void chipImpl(const char* text, ImU32 color, bool filled) {
    ImGui::PushFont(fonts().uiSemi, kMonoSm);
    ImVec2 textSize = ImGui::CalcTextSize(text);
    ImVec2 pad{8, 3};
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 min = pos, max = {pos.x + textSize.x + pad.x * 2, pos.y + textSize.y + pad.y * 2};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (filled) {
        dl->AddRectFilled(min, max, col::alpha(color, 0.18f), 3.0f);
        dl->AddRect(min, max, col::alpha(color, 0.75f), 3.0f, 1.0f);
    } else {
        dl->AddRect(min, max, col::alpha(color, 0.6f), 3.0f, 1.0f);
    }
    dl->AddText({min.x + pad.x, min.y + pad.y}, color, text);
    ImGui::Dummy({max.x - min.x, max.y - min.y});
    ImGui::PopFont();
}

void badge(const char* text, ImU32 color) { chipImpl(text, color, false); }
void badgeFilled(const char* text, ImU32 color) { chipImpl(text, color, true); }

void statusDot(ImU32 color, bool glowing) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float r = 4.0f;
    ImVec2 c{pos.x + r, pos.y + ImGui::GetTextLineHeight() * 0.55f};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (glowing) {
        float a = 0.25f + 0.35f * pulse(2.2f);
        dl->AddCircleFilled(c, r + 3.0f, col::alpha(color, a * 0.35f * cosmetics().glow));
    }
    dl->AddCircleFilled(c, r, color);
    ImGui::Dummy({r * 2 + 2, ImGui::GetTextLineHeight()});
}

void spinner(float radius, ImU32 color) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 c{pos.x + radius, pos.y + radius + 2};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = static_cast<float>(ImGui::GetTime()) * 6.0f;
    dl->PathArcTo(c, radius, t, t + 4.4f, 24);
    dl->PathStroke(color, 2.0f);
    ImGui::Dummy({radius * 2, radius * 2 + 4});
}

void verdictChip(guard::VerdictLevel level) {
    switch (level) {
        case guard::VerdictLevel::TrustedAuto: badgeFilled("AUTO-TRUSTED", col::Violet); break;
        case guard::VerdictLevel::Trusted: badgeFilled("TRUSTED", col::Success); break;
        case guard::VerdictLevel::StalePin: badgeFilled("CONTRACT CHANGED", col::Warn); break;
        case guard::VerdictLevel::ConstraintFail: badgeFilled("OUT OF BOUNDS", col::Danger); break;
        case guard::VerdictLevel::Unlisted: badge("UNLISTED", col::Steel); break;
    }
}

void riskLine(const guard::RiskFlag& flag) {
    ImU32 color = flag.severity == guard::RiskSeverity::Critical ? col::Danger
                  : flag.severity == guard::RiskSeverity::Warn   ? col::Warn
                                                                 : col::Steel;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float lh = ImGui::GetTextLineHeight();
    drawIcon(ImGui::GetWindowDrawList(),
             flag.severity == guard::RiskSeverity::Info ? Icon::Pulse : Icon::Warning,
             {pos.x + 8, pos.y + lh * 0.55f}, 13.0f, color, 1.6f);
    ImGui::Dummy({20, lh});
    ImGui::SameLine();
    ImGui::PushFont(fonts().ui, kTextSm);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(color));
    ImGui::TextWrapped("%s", flag.message.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void resourceBar(const char* label, double used, double max, const std::string& text) {
    ImGui::PushFont(fonts().uiSemi, kTextSm);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::SameLine();
    ImGui::PushFont(fonts().mono, kMonoSm);
    float tw = ImGui::CalcTextSize(text.c_str()).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - tw);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Ice));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    double pct = max > 0 ? used / max : 0.0;
    if (pct > 1.0) pct = 1.0;
    ImU32 color = pct > 0.9 ? col::Danger : pct > 0.75 ? col::Warn : col::Cyan;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = 5.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, {pos.x + w, pos.y + h}, col::rgba(0x0B1522), 2.5f);
    if (pct > 0.002) {
        float fillW = w * static_cast<float>(pct);
        dl->AddRectFilled(pos, {pos.x + fillW, pos.y + h}, col::alpha(color, 0.85f), 2.5f);
        if (cosmetics().glow > 0.01f)
            dl->AddRectFilled({pos.x, pos.y - 1}, {pos.x + fillW, pos.y + h + 1},
                              col::alpha(color, 0.12f * cosmetics().glow), 3.5f);
    }
    ImGui::Dummy({w, h + 6});
}

void emptyState(Icon icon, const char* title, const char* sub) {
    float w = ImGui::GetContentRegionAvail().x;
    vspace(28);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    drawIcon(ImGui::GetWindowDrawList(), icon, {pos.x + w * 0.5f, pos.y + 20}, 40.0f,
             col::alpha(col::Slate, 0.9f), 1.6f);
    vspace(52);
    ImGui::PushFont(fonts().uiSemi, kTextLg);
    float tw = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((w - tw) * 0.5f + ImGui::GetStyle().WindowPadding.x);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PushFont(fonts().ui, kTextSm);
    tw = ImGui::CalcTextSize(sub).x;
    ImGui::SetCursorPosX((w - tw) * 0.5f + ImGui::GetStyle().WindowPadding.x);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Slate));
    ImGui::TextUnformatted(sub);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    vspace(28);
}

// --- data display -----------------------------------------------------------

void kvRow(const char* key, const std::string& value, bool mono, bool copyable) {
    ImGui::PushID(key);
    ImGui::PushFont(fonts().ui, kTextSm);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
    ImGui::TextUnformatted(key);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine(150);
    if (mono)
        monoText(value, col::Ice, kMono);
    else
        ImGui::TextUnformatted(value.c_str());
    if (copyable) {
        ImGui::SameLine();
        if (iconButton("##copy", Icon::Copy, "Copy", col::Slate, 13.0f))
            ImGui::SetClipboardText(value.c_str());
    }
    ImGui::PopID();
}

bool beginFieldPair(const char* id) {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {5, 2});
    bool open = ImGui::BeginTable(id, layout().phone() ? 1 : 2,
                                  ImGuiTableFlags_SizingStretchSame |
                                      ImGuiTableFlags_NoPadOuterX);
    if (!open) ImGui::PopStyleVar();
    return open;
}

bool nextField() { return ImGui::TableNextColumn(); }

void endFieldPair() {
    ImGui::EndTable();
    ImGui::PopStyleVar();
}

void beginModalBody(const char* id, float footerReserve) {
    if (layout().phone()) {
        // The sheet owns the whole screen; the body takes everything above
        // the footer so the form scrolls and the buttons never move.
        ImGui::BeginChild(id, {0, -footerReserve}, ImGuiChildFlags_NavFlattened);
        return;
    }
    float cap = ImGui::GetMainViewport()->WorkSize.y * 0.9f - footerReserve -
                ImGui::GetCursorPosY();
    if (cap < 160.0f) cap = 160.0f;
    ImGui::SetNextWindowSizeConstraints({0, 0}, {FLT_MAX, cap});
    ImGui::BeginChild(id, {0, 0},
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened);
}

void endModalBody() { ImGui::EndChild(); }

void kvAsset(const char* key, const std::string& asset) {
    ImGui::PushID(key);
    ImGui::PushFont(fonts().ui, kTextSm);
    ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
    ImGui::TextUnformatted(key);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine(150);
    assetText(asset);
    ImGui::PopID();
}

static void jsonValueInline(const dwarfkit::json& v) {
    using dwarfkit::json;
    if (v.is_string()) {
        const std::string& s = v.get_ref<const std::string&>();
        // Assets get the app-wide amount/ticker treatment, even inside JSON.
        if (tb::guard::parseAsset(s).has_value())
            assetText(s, kMonoSm);
        else
            monoText("\"" + s + "\"", col::rgba(0xA8E6C9), kMonoSm);
    } else if (v.is_number()) {
        monoText(v.dump(), col::Cyan, kMonoSm);
    } else if (v.is_boolean()) {
        monoText(v.get<bool>() ? "true" : "false", col::Warn, kMonoSm);
    } else if (v.is_null()) {
        monoText("null", col::Slate, kMonoSm);
    } else {
        monoText(v.dump(), col::Ice, kMonoSm);
    }
}

void jsonTree(const dwarfkit::json& value, const char* id) {
    using dwarfkit::json;
    ImGui::PushID(id);
    std::function<void(const json&, const std::string&)> walk = [&](const json& v,
                                                                    const std::string& key) {
        if (v.is_object() || v.is_array()) {
            std::string label = key.empty() ? (v.is_object() ? "{}" : "[]") : key;
            char count[32];
            std::snprintf(count, sizeof count, "  (%zu)", v.size());
            ImGui::PushFont(fonts().mono, kMonoSm);
            bool open = ImGui::TreeNodeEx(label.c_str(),
                                          ImGuiTreeNodeFlags_SpanAvailWidth |
                                              (v.size() <= 8 ? ImGuiTreeNodeFlags_DefaultOpen
                                                             : 0),
                                          "%s%s", label.c_str(), count);
            ImGui::PopFont();
            if (open) {
                size_t index = 0;
                for (auto it = v.begin(); it != v.end(); ++it, ++index) {
                    std::string childKey =
                        v.is_object() ? it.key() : "[" + std::to_string(index) + "]";
                    const json& child = v.is_object() ? it.value() : *it;
                    if (child.is_object() || child.is_array()) {
                        walk(child, childKey);
                    } else {
                        ImGui::PushID(static_cast<int>(index));
                        ImGui::Indent(18);
                        ImGui::PushFont(fonts().mono, kMonoSm);
                        ImGui::PushStyleColor(ImGuiCol_Text, col::vec(col::Steel));
                        ImGui::TextUnformatted(childKey.c_str());
                        ImGui::PopStyleColor();
                        ImGui::PopFont();
                        ImGui::SameLine(0, 8);
                        jsonValueInline(child);
                        ImGui::Unindent(18);
                        ImGui::PopID();
                    }
                }
                ImGui::TreePop();
            }
        } else {
            jsonValueInline(v);
        }
    };
    walk(value, "");
    ImGui::PopID();
}

void drawQr(const std::string& text, float targetPx) {
    auto qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
    int n = qr.getSize();
    const int quiet = 3;
    float module = targetPx / static_cast<float>(n + quiet * 2);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float total = module * static_cast<float>(n + quiet * 2);
    // Light card behind dark modules: what scanners expect.
    dl->AddRectFilled(pos, {pos.x + total, pos.y + total}, col::rgba(0xE8F6FF), 4.0f);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (qr.getModule(x, y)) {
                float px = pos.x + module * static_cast<float>(x + quiet);
                float py = pos.y + module * static_cast<float>(y + quiet);
                dl->AddRectFilled({px, py}, {px + module + 0.4f, py + module + 0.4f},
                                  col::rgba(0x04121F));
            }
    ImGui::Dummy({total, total});
}

// --- adaptive layout helpers -------------------------------------------------

bool beginAdaptiveModal(const char* id, float desktopWidth) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const Layout& lay = layout();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        lay.phone() ? ImVec2(16, 14) : ImVec2(22, 20));
    // Opaque sheet: the page underneath must never ghost through a form.
    ImGui::PushStyleColor(ImGuiCol_PopupBg, col::vec(col::Bg));
    if (lay.phone()) {
        // Full-screen sheet: content fills the viewport minus safe areas.
        ImGui::SetNextWindowPos({vp->WorkPos.x, vp->WorkPos.y + lay.safeTop});
        ImGui::SetNextWindowSize(
            {vp->WorkSize.x, vp->WorkSize.y - lay.safeTop - lay.safeBottom});
        bool open = ImGui::BeginPopupModal(id, nullptr,
                                           ImGuiWindowFlags_NoResize |
                                               ImGuiWindowFlags_NoTitleBar |
                                               ImGuiWindowFlags_NoMove);
        if (!open) {
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
        return open;
    }
    float w = std::min(desktopWidth, vp->WorkSize.x - 60.0f);
    ImGui::SetNextWindowSize({w, 0});
    ImGui::SetNextWindowSizeConstraints({w, 0}, {w, vp->WorkSize.y - 60.0f});
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, {0.5f, 0.5f});
    bool open = ImGui::BeginPopupModal(id, nullptr,
                                       ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoTitleBar |
                                           ImGuiWindowFlags_NoMove);
    if (!open) {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
    return open;
}

void endAdaptiveModal() {
    ImGui::EndPopup();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

float pairWidth() {
    if (layout().phone()) return 0.0f;  // full width
    return (ImGui::GetContentRegionAvail().x - 12.0f) * 0.5f;
}

void maybeSameLine(float spacing) {
    if (layout().phone())
        vspace(8);
    else
        ImGui::SameLine(0, spacing);
}

void tooltip(const char* text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) return;
    ImGui::PushFont(fonts().ui, kTextSm);
    ImGui::SetTooltip("%s", text);
    ImGui::PopFont();
}


// --- drag & drop -------------------------------------------------------------

int acceptDropOnLastItem(const char* listId) {
    int from = -1;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(listId))
            from = *static_cast<const int*>(payload->Data);
        ImGui::EndDragDropTarget();
    }
    return from;
}

int dragGrip(const char* listId, int index, const char* preview) {
    float h = ImGui::GetFrameHeight() * 0.8f;
    float w = 18.0f * dpiScale();
    ImGui::InvisibleButton("##grip", {w, h});
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 color = ImGui::IsItemHovered() || ImGui::IsItemActive()
                      ? col::Cyan
                      : col::alpha(col::Slate, 0.8f);
    float cx = (min.x + max.x) * 0.5f, cy = (min.y + max.y) * 0.5f;
    float half = w * 0.28f;
    for (int i = -1; i <= 1; ++i)
        dl->AddLine({cx - half, cy + static_cast<float>(i) * 4.0f * dpiScale()},
                    {cx + half, cy + static_cast<float>(i) * 4.0f * dpiScale()}, color,
                    1.4f);
    ::ui::HandOnHover();
    tooltip("Drag to reorder");

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload(listId, &index, sizeof index);
        ImGui::PushFont(fonts().uiSemi, kTextSm);
        ImGui::TextUnformatted(preview && preview[0] ? preview : "Move here...");
        ImGui::PopFont();
        ImGui::EndDragDropSource();
    }
    return acceptDropOnLastItem(listId);
}

// --- toasts ------------------------------------------------------------------

void drawToasts(AppState& state) {
    if (state.toasts.empty()) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float x = vp->WorkPos.x + vp->WorkSize.x - 24;
    float y = vp->WorkPos.y + vp->WorkSize.y - 24;

    ImGui::PushFont(fonts().uiMedium, kText);
    for (auto it = state.toasts.rbegin(); it != state.toasts.rend(); ++it) {
        const Toast& t = *it;
        const float lifeMs = t.ttlSec * 1000.0f;
        const float ageMs = static_cast<float>(nowMs() - t.bornMs);

        // Slide in over the first 200ms, fade out over the last 300ms.
        float slide = cosmetics().reduceMotion ? 1.0f : easeOutCubic(ageMs / 200.0f);
        float fade = ageMs > lifeMs - 300.0f ? (lifeMs - ageMs) / 300.0f : 1.0f;
        if (fade < 0.0f) fade = 0.0f;

        ImU32 accent = t.kind == Toast::Success ? col::Success
                       : t.kind == Toast::Warn  ? col::Warn
                       : t.kind == Toast::Error ? col::Danger
                                                : col::Cyan;
        ImVec2 textSize = ImGui::CalcTextSize(t.text.c_str(), nullptr, false, 380.0f);
        ImVec2 pad{16, 12};
        ImVec2 box{textSize.x + pad.x * 2 + 8, textSize.y + pad.y * 2};
        float slideOff = (1.0f - slide) * 24.0f;
        ImVec2 min{x - box.x + slideOff, y - box.y};
        ImVec2 max{x + slideOff, y};

        dl->AddRectFilled(min, max, col::alpha(col::rgba(0x0B1422, 0xF2), fade), 4.0f);
        dl->AddRect(min, max, col::alpha(accent, 0.65f * fade), 4.0f, 1.0f);
        dl->AddRectFilled({min.x, min.y}, {min.x + 3, max.y}, col::alpha(accent, fade), 2.0f);
        dl->AddText(nullptr, 0.0f, {min.x + pad.x + 6, min.y + pad.y},
                    col::alpha(col::Ice, fade), t.text.c_str(), nullptr, 380.0f);

        float frac = 1.0f - ageMs / lifeMs;
        if (frac > 0)
            dl->AddLine({min.x + 3, max.y - 1}, {min.x + 3 + (box.x - 6) * frac, max.y - 1},
                        col::alpha(accent, 0.5f * fade), 2.0f);

        y = min.y - 10;
    }
    ImGui::PopFont();
}

}  // namespace tb::ui
