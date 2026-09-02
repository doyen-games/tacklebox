// TackleBox entry point. One SDL3 shell for every platform: Windows, Linux,
// macOS today; the same file drives the iOS/Android builds (GLES context,
// touch, lifecycle) via TB_MOBILE.
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#include <SDL3/SDL.h>
#ifdef TB_MOBILE
// SDL_main.h provides the JNI/UIKit entry point plumbing on mobile.
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#endif
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#ifdef _WIN32
#include <dwmapi.h>
#endif

#include "app/controller.hpp"
#include "app/state.hpp"
#include "core/clipboard.hpp"
#include "core/deeplink.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/task_runner.hpp"
#include "ui/app_ui.hpp"
#include "ui/layout.hpp"
#include "ui/qa.hpp"
#include "ui/theme.hpp"
#include "tb_version.h"

namespace {

tb::Controller* g_controller = nullptr;

// Dropped files route by extension: .tbx imports a vault, .wasm/.abi stage a
// contract deployment.
void handleDrop(const char* dropped) {
    if (!g_controller || !dropped) return;
    std::string path = dropped;
    auto endsWith = [&](const char* suffix) {
        std::string suf(suffix);
        return path.size() >= suf.size() &&
               path.compare(path.size() - suf.size(), suf.size(), suf) == 0;
    };
    if (endsWith(".tbx")) {
        g_controller->importVault(path);
    } else if (endsWith(".wasm") || endsWith(".abi")) {
        tb::AppState& state = g_controller->state();
        std::string wasm = endsWith(".wasm") ? path : state.deploy.wasmPath;
        std::string abi = endsWith(".abi") ? path : state.deploy.abiPath;
        g_controller->previewDeploy(wasm, abi);
        state.page = tb::Page::Contracts;
        g_controller->toast(tb::Toast::Info, "Deployment files staged (Contracts > DEPLOY)");
    } else {
        g_controller->toast(tb::Toast::Warn, "Drop .tbx vaults or .wasm/.abi contract files");
    }
}

}  // namespace

#if defined(_WIN32) && !defined(TB_MOBILE)
// Hybrid-graphics (NVIDIA Optimus / AMD PowerXpress) drivers read these
// exported globals when the GL context is created and route the app to the
// discrete GPU when nonzero. Set from the persisted preference in main(),
// before the window exists.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc, char** argv) {
    // Development form-factor overrides, so every chrome is testable anywhere.
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--phone")) tb::ui::layoutConfig().override_ = 0;
        if (!std::strcmp(argv[i], "--tablet")) tb::ui::layoutConfig().override_ = 1;
        if (!std::strcmp(argv[i], "--desktop")) tb::ui::layoutConfig().override_ = 2;
        if (!std::strcmp(argv[i], "--touch")) tb::ui::layoutConfig().forceTouch = true;
        if (!std::strcmp(argv[i], "--qa-shots") && i + 1 < argc)
            tb::ui::qa::configure(argv[++i]);
        if (!std::strcmp(argv[i], "--data-dir") && i + 1 < argc)
            tb::overrideDataDir(argv[++i]);
    }

    // Deep links: a browser (or the wharfkit plugin) launched us with a
    // tacklebox:/esr: uri. If another instance already owns this data dir,
    // hand the uri over and bow out before any window exists.
    std::optional<std::string> launchUri = tb::deeplink::uriFromArgs(argc, argv);
    tb::deeplink::InstanceServer instanceServer;
#ifndef TB_MOBILE
    if (!instanceServer.claim()) {
        const std::string forward = launchUri.value_or("tacklebox://open");
        if (tb::deeplink::forwardToPrimaryInstance(forward)) {
            std::printf("forwarded %s to the running TackleBox\n", forward.c_str());
        } else {
            std::fprintf(stderr, "another TackleBox is running but unreachable; exiting\n");
        }
        return 0;
    }
#endif

    // Cosmetics load early: the GPU preference must be set before the GL
    // context exists (drivers read the exports at context init).
    tb::ui::loadCosmetics();
#if defined(_WIN32) && !defined(TB_MOBILE)
    NvOptimusEnablement = tb::ui::cosmetics().preferHighPerfGpu ? 1 : 0;
    AmdPowerXpressRequestHighPerformance = tb::ui::cosmetics().preferHighPerfGpu ? 1 : 0;
#endif

    SDL_SetAppMetadata("TackleBox", TB_VERSION, "io.tacklebox.wallet");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "fatal: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

#ifdef TB_MOBILE
    // OpenGL ES 3.0 on iOS/Android.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    const char* glslVersion = "#version 300 es";
    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN |
                         SDL_WINDOW_HIGH_PIXEL_DENSITY;
#else
    // Desktop GL 3.2 core: the floor macOS still ships.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    const char* glslVersion = "#version 150";
    Uint32 windowFlags =
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int winW = 1280, winH = 820;
    if (tb::ui::layoutConfig().override_ == 0) {
        winW = 400;
        winH = 780;
    } else if (tb::ui::layoutConfig().override_ == 1) {
        winW = 840;
        winH = 760;
    }
    SDL_Window* window = SDL_CreateWindow("TackleBox", winW, winH, windowFlags);
    if (!window) {
        std::fprintf(stderr, "fatal: window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
#ifndef TB_MOBILE
    SDL_SetWindowMinimumSize(window, 360, 600);
#endif

#ifdef _WIN32
    // Dark titlebar on Windows 10 20H1+ / 11.
    if (HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                 SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)) {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
    }
#endif

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        std::fprintf(stderr, "fatal: GL context failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);

    // What is actually rendering: shown in Settings > About, and the basis
    // for the software-rasterizer warning there.
    const char* glRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* glVendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    std::string rendererInfo = std::string(glRenderer ? glRenderer : "unknown") + "  (" +
                               (glVendor ? glVendor : "unknown") + ")";
    tb::Log::info("renderer: %s", rendererInfo.c_str());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    float scale = SDL_GetWindowDisplayScale(window);
    if (scale <= 0.0f) scale = 1.0f;
    tb::ui::initTheme(scale);

    ImGui_ImplSDL3_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init(glslVersion);

    tb::installClipboard([](const std::string& text) { SDL_SetClipboardText(text.c_str()); },
                         [] {
                             char* text = SDL_GetClipboardText();
                             std::string out = text ? text : "";
                             SDL_free(text);
                             return out;
                         });

#ifdef TB_MOBILE
    // App-sandbox storage; there is no $HOME on Android/iOS.
    if (char* pref = SDL_GetPrefPath("tacklebox", "TackleBox")) {
        tb::overrideDataDir(pref);
        SDL_free(pref);
    }
#endif

    tb::AppState state;
    state.gpuRenderer = rendererInfo;
    for (const char* soft : {"llvmpipe", "softpipe", "SwiftShader", "Microsoft Basic Render",
                             "GDI Generic", "Software Rasterizer"})
        if (rendererInfo.find(soft) != std::string::npos) state.gpuSoftware = true;
    tb::TaskRunner runner;
    tb::Controller controller(state, runner);
    controller.init();
    g_controller = &controller;

    tb::Log::info("TackleBox up (SDL shell) - vault %s",
                  state.vaultExists ? "found" : "not created yet");

#ifndef TB_MOBILE
    // Dapp deep links: register the url schemes, raise the window when a
    // request lands, accept uris forwarded by second launches, and route the
    // uri this launch may have carried.
    controller.setRaiseWindow([window] {
        SDL_RaiseWindow(window);
        SDL_FlashWindow(window, SDL_FLASH_UNTIL_FOCUSED);
    });
    tb::deeplink::registerSchemes();
    instanceServer.start([&runner, &controller](std::string uri) {
        runner.postMain([&controller, uri = std::move(uri)] { controller.handleDeepLink(uri); });
    });
    if (launchUri) controller.handleDeepLink(*launchUri);
#endif

    bool running = true;
    bool textInputActive = false;
    int swapInterval = 1;
    while (running) {
        const Uint64 frameStartNs = SDL_GetTicksNS();
        bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        bool busy = runner.pending() > 0 || state.signPrompt || state.pluginPrompt ||
                    !state.toasts.empty() || !state.pipelineStatus.empty();

        // Framerate policy (Settings > Performance): each state has its own
        // cap; 0 means display sync, background -1 means match the focused
        // cap. Animations stay smooth in the background by default (30).
        int cap = focused ? tb::ui::cosmetics().fpsFocused
                          : (tb::ui::cosmetics().fpsBackground < 0
                                 ? tb::ui::cosmetics().fpsFocused
                                 : tb::ui::cosmetics().fpsBackground);
        int wantSwap = cap == 0 ? 1 : 0;  // caps pace manually; sync otherwise
        if (wantSwap != swapInterval) {
            SDL_GL_SetSwapInterval(wantSwap);
            swapInterval = wantSwap;
        }

        SDL_Event event;
        // Unfocused and quiet: sleep in the event wait for the frame budget
        // so a click or deep link still wakes the loop instantly.
        if (!focused && !busy) {
            int budgetMs = cap > 0 ? (1000 + cap - 1) / cap : 16;
            if (SDL_WaitEventTimeout(&event, budgetMs)) {
                do {
                    ImGui_ImplSDL3_ProcessEvent(&event);
                    if (event.type == SDL_EVENT_QUIT) running = false;
                    if (event.type == SDL_EVENT_DROP_FILE) handleDrop(event.drop.data);
                } while (SDL_PollEvent(&event));
            }
        } else {
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                switch (event.type) {
                    case SDL_EVENT_QUIT:
                        running = false;
                        break;
                    case SDL_EVENT_DROP_FILE:
                        handleDrop(event.drop.data);
                        break;
                    case SDL_EVENT_DID_ENTER_BACKGROUND:
                    case SDL_EVENT_WILL_ENTER_BACKGROUND:
                        // Mobile lifecycle: the OS may snapshot the screen and
                        // keep the process for days. Seal immediately.
                        if (state.unlocked && state.vault.security.lockOnBackground) {
                            controller.lockVault(true);  // mobile: always seal
                            tb::Log::info("locked on background transition");
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        runner.drainMain();
        controller.tick();
        if (!tb::ui::qa::beforeFrame(state, controller)) running = false;

        // Any real input feeds the auto-lock timer.
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0 || ImGui::IsAnyMouseDown() ||
            io.InputQueueCharacters.Size > 0 || io.MouseWheel != 0)
            controller.noteActivity();

        // Drive the OS keyboard on touch platforms.
        if (io.WantTextInput != textInputActive) {
            textInputActive = io.WantTextInput;
            if (textInputActive)
                SDL_StartTextInput(window);
            else
                SDL_StopTextInput(window);
        }

        // Safe-area intrusions (notches, home indicators) inset the chrome.
        SDL_Rect safe{};
        float safeTop = 0.0f, safeBottom = 0.0f;
        if (SDL_GetWindowSafeArea(window, &safe)) {
            int fullH = 0;
            SDL_GetWindowSize(window, nullptr, &fullH);
            safeTop = static_cast<float>(safe.y);
            safeBottom = static_cast<float>(fullH - (safe.y + safe.h));
            if (safeBottom < 0) safeBottom = 0;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        tb::ui::updateLayout(safeTop, safeBottom);

        tb::ui::drawApp(state, controller);

        ImGui::Render();
        int fbWidth = 0, fbHeight = 0;
        SDL_GetWindowSizeInPixels(window, &fbWidth, &fbHeight);
        glViewport(0, 0, fbWidth, fbHeight);
        glClearColor(0.016f, 0.027f, 0.047f, 1.0f);  // col::Void
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        tb::ui::qa::capture(window);
        SDL_GL_SwapWindow(window);

        // Manual pacing for capped modes (the event wait above already spent
        // part of the budget; only the remainder sleeps).
        if (cap > 0 && !tb::ui::qa::active()) {
            const Uint64 budgetNs = 1000000000ull / static_cast<Uint64>(cap);
            const Uint64 spentNs = SDL_GetTicksNS() - frameStartNs;
            if (spentNs < budgetNs)
                SDL_Delay(static_cast<Uint32>((budgetNs - spentNs) / 1000000ull));
        }
    }

    instanceServer.stop();
    controller.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
