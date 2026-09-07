// Form-factor system. One codebase, three chromes:
//   Phone   (<640 logical px wide): bottom tab bar + "more" sheet, sheets
//           instead of centered modals, single column, touch-sized targets.
//   Tablet  (640-1000): icon navigation rail, up to two columns.
//   Desktop (>1000): full sidebar, the classic layout.
//
// The factor is computed each frame from the viewport (overridable from the
// command line / settings so every layout can be exercised on any machine).
// On touch builds targets grow regardless of factor.
#pragma once

#include <imgui.h>

namespace tb::ui {

enum class FormFactor { Phone, Tablet, Desktop };

struct Layout {
    FormFactor factor = FormFactor::Desktop;
    bool touch = false;      // touch-first platform (mobile builds set this)
    float safeTop = 0.0f;    // OS intrusions (notch, status bar, home bar)
    float safeBottom = 0.0f;

    bool phone() const { return factor == FormFactor::Phone; }
    bool tablet() const { return factor == FormFactor::Tablet; }
    bool desktop() const { return factor == FormFactor::Desktop; }

    // How many side-by-side columns page layouts should use.
    int columns() const { return phone() ? 1 : 2; }
    // Minimum comfortable hit target height.
    float hit() const { return touch ? 46.0f : phone() ? 42.0f : 38.0f; }
    // Content padding for the routed page.
    ImVec2 pagePadding() const {
        return phone() ? ImVec2(14, 14) : tablet() ? ImVec2(20, 18) : ImVec2(28, 24);
    }
};

// -1 = auto; else forces a FormFactor. Persisted in cosmetics, and the
// --phone/--tablet/--touch CLI flags set it for development.
struct LayoutConfig {
    int override_ = -1;
    bool forceTouch = false;
    int textSizePct = 0;  // --text-size <pct>: overrides the cosmetic for QA runs; 0 = settings
};
LayoutConfig& layoutConfig();

// Recompute from the current viewport; call once per frame before drawApp.
void updateLayout(float safeTop = 0.0f, float safeBottom = 0.0f);
const Layout& layout();

// The routed page's screen rect, recorded by the shell each frame before the
// page draws: popups anchored near the end of a page flip upward when they
// would run past pageBottom().
void setPageRect(const ImVec2& min, const ImVec2& max, float scrollY);
float pageTop();
float pageBottom();
float pageScroll();  // the routed page's current vertical scroll offset

// Ask the shell to scroll the routed page to `y` (FLT_MAX = the bottom) over
// the next frames, so content appended to a page comes into view instead of
// landing below the fold. takePageScrollRequest() is -1 when nothing is due.
void requestPageScroll(float y);
float takePageScrollRequest();

}  // namespace tb::ui
