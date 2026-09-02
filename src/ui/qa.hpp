// Visual QA harness: `tacklebox --qa-shots <dir>` boots an offline fixture
// state, walks every screen for a few frames, saves one PNG per page into
// <dir>, and exits. Combine with --phone/--tablet for per-form-factor decks.
// This is the standing review process for UI work (see CLAUDE.md): run it,
// read the deck, fix spacing, run it again.
#pragma once

#include <string>

struct SDL_Window;

namespace tb {
struct AppState;
class Controller;
}  // namespace tb

namespace tb::ui::qa {

void configure(const std::string& outDir);  // from --qa-shots
bool active();
// Page scroll the active step wants (applied by the shell every frame while
// the step settles); negative = leave the page at the top.
float pageScrollY();
// Interaction hooks: surfaces that are invisible until clicked (dropdowns,
// menus, palettes) check these so the tour can screenshot them open.
// forceOpen(tag) is true while the active step requests `tag` (one caller
// per frame wins, so a tag shared by several widgets opens only the first).
bool forceOpen(const char* tag);
// Non-consuming peek at the active step's tag: for prerequisites of the
// forceOpen site (switching to the tab that hosts the combo).
bool wantsOpen(const char* tag);
// Force the next combo with this label open (call right before BeginCombo,
// same ID stack). QA-only; uses imgui internals.
void openCombo(const char* label);

// Before NewFrame: advance the tour (inject fixtures, flip pages).
// Returns false when the tour is finished and the app should quit.
bool beforeFrame(AppState& state, Controller& controller);

// After RenderDrawData, before SwapWindow: capture when a shot is due.
void capture(SDL_Window* window);

}  // namespace tb::ui::qa
