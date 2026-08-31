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
};
LayoutConfig& layoutConfig();

// Recompute from the current viewport; call once per frame before drawApp.
void updateLayout(float safeTop = 0.0f, float safeBottom = 0.0f);
const Layout& layout();

}  // namespace tb::ui
