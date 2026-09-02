# TackleBox project instructions

# ImGui UI Blueprint

## 0. Context (filled for this repo)

* ImGui version: 1.93.0 WIP (vendored snapshot at vendor/imgui, commit 0af8f47) | docking branch: no
* C++ standard: C++20 (dwarfkit requires it)
* Base font size: kText = 17 px (Rajdhani Medium default; Share Tech Mono for data) | DPI: ui::gScale set at boot in tb::ui::initTheme from SDL_GetWindowDisplayScale
* Typical window size(s): 1280x820 desktop; 840x760 tablet; 400x780 phone (`--tablet` / `--phone --touch` flags) | resizable: yes, min 360x600; form factor recomputed per frame (src/ui/layout.hpp)
* Style: custom Tron-dark theme in src/ui/theme.cpp (never assume default dark; palette in theme.hpp col::)

## 1. Laws

1. No magic numbers. All positions and sizes derive from GetContentRegionAvail, CalcTextSize, GetFrameHeight, GetTextLineHeightWithSpacing, or style vars.
2. All layout goes through ui_helpers.h (section 8). If a helper is missing, add one helper; never inline the same layout math twice.
3. Layout must survive resize. Verify mentally at half and double window width before returning code.
4. Rows and grids use BeginTable with SizingStretchProp or SizingStretchSame. Never Columns, never SameLine chains past 2 items.
5. Spacing changes use PushStyleVar (ItemSpacing, FramePadding, WindowPadding) or ui::VSpace(rows). Never pixel-tuned Dummy calls.
6. Every widget created in a loop gets PushID(i). Duplicate labels get a "##suffix". No ID collisions.
7. The only raw pixel literal allowed is one wrapped in ui::S(px), so DPI scaling stays centralized.

## 2. Text

* Body copy: ui::BodyText. Wraps at 90% of avail, never widens the layout.
* Centered headings and labels: ui::CenteredText.
* Text in fixed cells must clip, never grow the row: use a table column or child region.
* Dynamic strings in tables: WidthStretch column + TextUnformatted; the cell clips automatically.

## 3. Buttons and inputs

* Default size: ui::ButtonSize (min width S(120), height 1.4 * frame height).
* Single centered CTA: ui::CenteredButton.
* Equal-width rows: ui::BeginButtonRow(n) + ui::CellButton per button + ui::EndButtonRow.
* Right-aligned dialog actions: ui::RightNext(total width) before the first button.
* Input widths: SetNextItemWidth(-FLT_MIN) to fill, or a fraction of avail. Never fixed px.

## 4. Regions

* Scrolling lists and logs: ui::BeginScrollRegion(id, rows). Height in rows, not px.
* Modals and popups: ui::NextWindowCentered before OpenPopup, size as viewport fraction.
* Section dividers: SeparatorText; ui::VSpace(1) between logical groups.

## 5. Output contract

* Return only the requested Begin/End body or a minimal diff. No prose unless asked.
* Must compile against the version pinned in section 0. No imgui_internal.h unless requested.
* New helpers appear as a diff to ui_helpers.h.
* Self-check before returning: resize-safe, helpers only, IDs unique, no raw px outside ui::S.
* Screenshot QA is mandatory after UI changes and when creating any new menu:
  run `tacklebox --qa-shots <dir>` (and again with `--phone --touch` /
  `--tablet`), READ the affected pages' PNGs, fix padding/spacing/centering/
  overflow findings, regenerate, verify. New pages must be added to the tour
  in src/ui/qa.cpp (kSteps + fixtures) in the same change.
* QA must exercise surfaces that are invisible until interacted with -
  dropdowns, popup menus, modals, palettes. A step opens one with
  `qa::forceOpen("tag")` at the trigger site (dropdown/menu OpenPopup) or
  `qa::openCombo("##id")` right before a `BeginCombo`, then names the tag in
  the step's enter(). `qa::wantsOpen("tag")` (non-consuming) flips any
  prerequisite (the tab hosting the combo) in the same step. Any new
  dropdown/combo/popup gets a QA step in the same change - the tour must
  cover EVERY modal and dropdown in the app. Force-opened popups need an
  explicit `SetNextWindowPos` (no hovered trigger to anchor to; wrap it in
  `qa::active()`), steps that do not set a scroll offset start at the top,
  and cross-page modals (the rule editor) are reset in showShellPage so
  steps stay isolated.

## 6. Iteration protocol

1. User pastes a screenshot plus deltas as one-liners: "element: actual vs expected".
2. Return the minimal diff fixing exactly those deltas, nothing else.
3. If a delta is ambiguous (which element or axis), ask one question, otherwise proceed.

## 7. Per-screen request template

Build \<screen name>: \<ordered element list, with exact strings for any labels>. Follow blueprint. Output: one function void Draw\<Screen>().

## 8. ui_helpers.h (the sole layout API)

The header lives at **src/ui/ui_helpers.h** - that file is the canon; read it
before writing UI code and extend it by diff, never fork the math elsewhere.
ui::gScale is set in tb::ui::initTheme (same value as tb::ui::dpiScale()).

## Repo notes on top of the blueprint

* Layer division: `ui_helpers.h` = layout math (the blueprint API).
  `src/ui/widgets.hpp` = TackleBox's styled widgets (neonButton, textField,
  toggle, chips, cards, jsonTree, drawQr...) - use these for anything the
  user sees; their internals must obey the laws. `src/ui/layout.hpp` = form
  factors (Phone/Tablet/Desktop) with `pairWidth()/maybeSameLine()` for
  responsive two-ups and `beginAdaptiveModal/endAdaptiveModal` for modals
  that become full-screen sheets on phones - prefer it over
  ui::NextWindowCentered for anything that ships on mobile.
* Hand-drawn chrome via ImDrawList (sidebar, rail, bottom tab bar, fx.cpp
  icons, glow) is not item layout; the BeginTable law does not apply there.
* Views written before this blueprint contain violations; bring a view up to
  canon when touching it, do not mass-rewrite untouched views.
* ImGui 1.92.8+ API notes: PushFont(font, size); AddRect/PathStroke take
  thickness before flags; ImGuiChildFlags_Borders; dynamic fonts (no size
  baked into the atlas).

## Build / test (this machine: Windows 11, MinGW GCC 16, Ninja)

* Configure: `cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++`
* Build: `cmake --build build` (kill a running tacklebox.exe first; Windows locks the image at link time)
* Tests: `./build/tacklebox_tests.exe` (doctest; must stay green)
* Form factors: `tacklebox --phone --touch | --tablet | --desktop`

## Non-negotiables

* Every signing path goes through Controller::guardedSign; never add a signing route that bypasses the guard.
* Nothing sensitive leaves the encrypted vault; no telemetry, chain/AA endpoints only, https-only (localhost excepted).
* Vendored dwarfkit is patched ONLY via cmake/CompilerWarnings.cmake (documented in vendor/dwarfkit/VENDORED_COMMIT.txt).
