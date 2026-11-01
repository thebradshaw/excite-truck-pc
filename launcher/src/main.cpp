// Excite PC launcher: choose Excite Truck or Excitebots, set them up from your discs, change
// their settings, then start the game.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "launcher_config.h"
#include "setup_job.h"
#include "ui.h"

namespace cfg = launcher_config;

namespace {

void SetGameIcon(SDL_Window* window) {
    std::vector<unsigned char> rgba;
    if (!cfg::LoadGameIcon(GameId::Truck, rgba) && !cfg::LoadGameIcon(GameId::Bots, rgba)) {
        return;
    }
    if (SDL_Surface* icon = SDL_CreateSurfaceFrom(48, 48, SDL_PIXELFORMAT_ABGR8888, rgba.data(), 48 * 4)) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }
}

// Starts the game in the background. Returns an empty string, or what went wrong.
std::string LaunchGame() {
    const GameId id = UiSelectedGame();
    const auto game = GameExecutable(id);
    if (game.empty()) {
        return std::string(Game(id).title) + " isn't installed in " + cfg::PathToUtf8(InstallDir(id)) +
               ". Go back to All Games and set it up again.";
    }
    if (!cfg::DiscFound()) {
        return "The game files from your disc aren't at " + cfg::PathToUtf8(cfg::DiscFolder()) +
               ". Set the game up again from All Games, or set dvd_root in " + cfg::PathToUtf8(cfg::ConfigPath()) +
               ".";
    }
    const std::string exe = cfg::PathToUtf8(game);
    const std::string dir = cfg::PathToUtf8(game.parent_path());
    const char* args[] = {exe.c_str(), nullptr};

    SDL_Environment* env = SDL_CreateEnvironment(true);
    if (const char* music = UiMusicFolderForGame(); music && *music) {
        SDL_SetEnvironmentVariable(env, "WC_MUSIC_DIR", music, true);
    }
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)args);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, dir.c_str());
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    if (process == nullptr) {
        return std::string("The game could not be started: ") + SDL_GetError();
    }
    SDL_DestroyProcess(process);   // the game keeps running on its own
    return {};
}

} // namespace

// Closing while a game is being set up stops the setup: ask first.
bool ConfirmClose(SDL_Window* window) {
    if (!SetupBusy()) return true;
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Keep setting up"},
        {0, 1, "Stop and close"},
    };
    const SDL_MessageBoxData box = {SDL_MESSAGEBOX_WARNING, window, "Excite PC",
                                    "A game is still being set up. Closing the launcher stops the setup; setting it "
                                    "up again later picks up where it stopped.",
                                    2, buttons, nullptr};
    int chosen = 0;
    return SDL_ShowMessageBox(&box, &chosen) && chosen == 1;
}

int main(int argc, char** argv) {
    SDL_SetHint(SDL_HINT_APP_NAME, "Excite PC");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Excite PC", SDL_GetError(), nullptr);
        return 1;
    }

    // A 16:9 window about three quarters the size of the screen.
    int width = 1280, height = 720;
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w > 0 && usable.h > 0) {
        width = std::clamp(usable.w * 3 / 4, 960, 1920);
        height = width * 9 / 16;
        if (height > usable.h * 9 / 10) {
            height = usable.h * 9 / 10;
            width = height * 16 / 9;
        }
    }
    if (const char* size = SDL_getenv("ET_LAUNCHER_SIZE")) {   // for testing, e.g. 960x540
        SDL_sscanf(size, "%dx%d", &width, &height);
    }
    SDL_Window* window = SDL_CreateWindow("Excite PC", width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Excite PC", SDL_GetError(), nullptr);
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 800, 450);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SetGameIcon(window);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Excite PC", SDL_GetError(), window);
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    UiInit(window, renderer);
    // A disc image passed in (the setup program forwards one dropped on it).
    if (argc > 1 && argv[1] && *argv[1]) {
        UiDropFile(argv[1], -1.0f);
    }

    bool running = true;
    bool shown = false;
    const Uint64 start = SDL_GetTicks();
    const char* screenshot = SDL_getenv("ET_LAUNCHER_SCREENSHOT");   // for testing: save a BMP and quit
    int frames = 0;
    while (running) {
        SDL_Event event;
        const bool idle = (SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED)) != 0;
        if (idle && SDL_WaitEventTimeout(&event, 250)) {
            SDL_PushEvent(&event);
        }
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    if (ConfirmClose(window)) running = false;
                    break;
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    if (event.window.windowID == SDL_GetWindowID(window) && ConfirmClose(window)) running = false;
                    break;
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                    UiRefresh();
                    break;
                case SDL_EVENT_GAMEPAD_ADDED:
                    SDL_OpenGamepad(event.gdevice.which);   // read by the menu navigation
                    break;
                case SDL_EVENT_DROP_FILE:
                    if (event.drop.data) UiDropFile(event.drop.data, event.drop.x);
                    break;
                default:
                    break;
            }
        }

        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        if (UiUpdateFonts(float(w), float(h))) {
            ImGui_ImplSDLRenderer3_DestroyFontsTexture();
            ImGui_ImplSDLRenderer3_CreateFontsTexture();
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        UiFrame(float(SDL_GetTicks() - start) / 1000.0f);
        ImGui::Render();

        SDL_SetRenderDrawColor(renderer, 20, 40, 80, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        if (screenshot && ++frames == 4) {
            if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr)) {
                SDL_SaveBMP(shot, screenshot);
                SDL_DestroySurface(shot);
            }
            running = false;
        }
        SDL_RenderPresent(renderer);
        if (!shown) {
            SDL_ShowWindow(window);
            shown = true;
        }

        switch (UiTakeAction()) {
            case UiAction::Launch: {
                const std::string error = LaunchGame();
                if (error.empty()) {
                    // Close, unless the other game is still being set up: then step aside.
                    if (SetupBusy()) {
                        SDL_MinimizeWindow(window);
                    } else {
                        running = false;
                    }
                } else {
                    UiShowError("Can't start the game", error.c_str());
                }
                break;
            }
            case UiAction::Quit:
                running = false;
                break;
            case UiAction::None:
                break;
        }
    }

    SetupStopAll();
    UiShutdown();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
