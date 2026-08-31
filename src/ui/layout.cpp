#include "ui/layout.hpp"

#include "ui/theme.hpp"

namespace tb::ui {

LayoutConfig& layoutConfig() {
    static LayoutConfig config;
    return config;
}

namespace {
Layout g_layout;
}

void updateLayout(float safeTop, float safeBottom) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float w = vp->WorkSize.x;

    LayoutConfig& config = layoutConfig();
    if (config.override_ == 0)
        g_layout.factor = FormFactor::Phone;
    else if (config.override_ == 1)
        g_layout.factor = FormFactor::Tablet;
    else if (config.override_ == 2)
        g_layout.factor = FormFactor::Desktop;
    else
        g_layout.factor = w < 640.0f  ? FormFactor::Phone
                          : w < 1000.0f ? FormFactor::Tablet
                                        : FormFactor::Desktop;

#ifdef TB_MOBILE  // defined by the iOS/Android build configurations
    g_layout.touch = true;
#else
    g_layout.touch = config.forceTouch;
#endif
    g_layout.safeTop = safeTop;
    g_layout.safeBottom = safeBottom;

    // Frame-level style adjustments the whole tree inherits.
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontScaleMain = g_layout.phone() ? 1.05f : 1.0f;
    style.TouchExtraPadding = g_layout.touch ? ImVec2(4, 4) : ImVec2(0, 0);
    style.ScrollbarSize = g_layout.touch ? 6.0f : 10.0f;
}

const Layout& layout() { return g_layout; }

}  // namespace tb::ui
