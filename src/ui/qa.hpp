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

// Before NewFrame: advance the tour (inject fixtures, flip pages).
// Returns false when the tour is finished and the app should quit.
bool beforeFrame(AppState& state, Controller& controller);

// After RenderDrawData, before SwapWindow: capture when a shot is due.
void capture(SDL_Window* window);

}  // namespace tb::ui::qa
