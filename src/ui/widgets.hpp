// The TackleBox widget kit. Custom-drawn controls sharing one voice: angular
// frames, hairline borders, cyan light. All stateless unless noted; views own
// their buffers.
#pragma once

#include <string>

#include <dwarfkit/core/json.hpp>

#include "app/state.hpp"
#include "guard/engine.hpp"
#include "ui/fx.hpp"
#include "ui/theme.hpp"

namespace tb::ui {

// --- typography -------------------------------------------------------------
void heading(const char* text, float size = kH1);
void subtext(const char* text);                       // steel, small
void sectionTitle(const char* text);                  // spaced caps + hairline
void monoText(const std::string& text, ImU32 color = col::Ice, float size = kMono);
void vspace(float px);
// Money gets ONE look app-wide: the quantity in col::Amount, the ticker in
// col::Ticker, mono. Anything shaped like "12.3456 WAX" splits; any other
// string renders whole in the amount color. dim mutes both (secondary rows).
void assetText(const std::string& asset, float size = kMono, bool dim = false);

// --- containers -------------------------------------------------------------
// A bordered panel. width 0 = full available width. Always pair with endCard.
bool beginCard(const char* id, float width = 0.0f, bool brackets = false);
void endCard();

// --- controls ---------------------------------------------------------------
enum class BtnKind { Primary, Ghost, Danger, Subtle };
bool neonButton(const char* label, BtnKind kind = BtnKind::Primary, ImVec2 size = {0, 0},
                bool disabled = false);
bool iconButton(const char* id, Icon icon, const char* tip, ImU32 color = col::Steel,
                float size = 16.0f);
// Hold-to-confirm. `progress` lives at the call site (static float). Returns
// true exactly once when the hold completes.
bool holdButton(const char* label, float holdSec, float* progress, ImVec2 size,
                ImU32 color = col::Danger);

struct FieldOpts {
    const char* placeholder = nullptr;
    bool password = false;
    bool mono = false;
    const char* error = nullptr;  // renders below in danger red
    const char* hint = nullptr;   // renders below in steel
    float width = 0.0f;           // 0 = fill available
    ImGuiInputTextFlags flags = 0;
    bool autoFocus = false;
};
// Returns true when Enter was pressed inside the field.
bool textField(const char* label, char* buf, size_t bufSize, const FieldOpts& opts = {});

bool toggle(const char* label, bool* value, const char* sub = nullptr);

// --- indicators -------------------------------------------------------------
void badge(const char* text, ImU32 color);
void badgeFilled(const char* text, ImU32 color);
void statusDot(ImU32 color, bool glowing = false);
void spinner(float radius, ImU32 color = col::Cyan);
void verdictChip(guard::VerdictLevel level);
void riskLine(const guard::RiskFlag& flag);
void resourceBar(const char* label, double used, double max, const std::string& text);
void emptyState(Icon icon, const char* title, const char* sub);

// --- data display -----------------------------------------------------------
// Key/value row; copyable adds a copy icon (plain clipboard, non-sensitive).
void kvRow(const char* key, const std::string& value, bool mono = false,
           bool copyable = false);
// kvRow for money: the value renders through assetText.
void kvAsset(const char* key, const std::string& asset);

// Scrollable body for a modal form with a pinned footer (save/cancel row).
// Desktop: auto-height capped to the viewport. Phone sheet: fills all the
// remaining height, leaving exactly footerReserve. Always pair with
// endModalBody before drawing the footer.
void beginModalBody(const char* id, float footerReserve);
void endModalBody();
void jsonTree(const dwarfkit::json& value, const char* id);
void drawQr(const std::string& text, float targetPx);

void tooltip(const char* text);

// Layout math lives in src/ui/ui_helpers.h (the blueprint's ui:: namespace);
// this header only declares TackleBox's styled widgets and responsive
// adaptations on top of it.

// --- adaptive layout helpers ------------------------------------------------
// Centered fixed-width modal on desktop/tablet; a full-screen sheet on phone
// (respecting safe areas). Call OpenPopup(id) yourself as before; pair every
// true return with endAdaptiveModal().
bool beginAdaptiveModal(const char* id, float desktopWidth);
void endAdaptiveModal();

// Width for one card in a side-by-side pair: half the row on wide layouts,
// the full row on phones (callers pair it with maybeSameLine()).
float pairWidth();
// SameLine on wide layouts, new row on phones.
void maybeSameLine(float spacing = 12.0f);

// --- drag & drop (reorder with memory) ---------------------------------------
// A grip handle (three bars) that is both drag source and drop target for
// list `listId`. `preview` labels the drag ghost. Returns the dragged row's
// source index when another row is dropped on this handle, else -1. Callers
// splice (from -> this row's index) and persist.
int dragGrip(const char* listId, int index, const char* preview);
// Widen the drop zone: attach a target for `listId` to the last submitted
// item (a whole card, a row). Returns the dragged source index on drop.
int acceptDropOnLastItem(const char* listId);

// --- overlays ---------------------------------------------------------------
void drawToasts(AppState& state);

}  // namespace tb::ui
