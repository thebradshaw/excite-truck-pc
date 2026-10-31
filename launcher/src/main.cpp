// Excite Truck launcher: opens a window with the main menu and settings, then starts the game
// (ExciteTruck.exe next to it) and closes.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "launcher_config.h"
#include "ui.h"

namespace cfg = launcher_config;

namespace {

void SetGameIcon(SDL_Window* window) {
    std::vector<unsigned char> rgba;
    if (!cfg::LoadGameIcon(rgba)) {
        return;
    }
    if (SDL_Surface* icon = SDL_CreateSurfaceFrom(48, 48, SDL_PIXELFORMAT_ABGR8888, rgba.data(), 48 * 4)) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }
}

// Starts the game in the background. Returns an empty string, or what went wrong.
std::string LaunchGame() {
    const auto game = cfg::GameExecutable();
    if (game.empty()) {
        return "ExciteTruck isn't in the launcher's folder. Run the build script again to put it back.";
    }
    if (!cfg::DiscFound()) {
        return "The game files from your disc aren't at " + cfg::PathToUtf8(cfg::DiscFolder()) +
               ". Run the setup again with your disc image, or set dvd_root in " + cfg::PathToUtf8(cfg::ConfigPath()) +
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

int main(int, char**) {
    SDL_SetHint(SDL_HINT_APP_NAME, "Excite Truck");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Excite Truck", SDL_GetError(), nullptr);
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
    SDL_Window* window = SDL_CreateWindow("Excite Truck", width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Excite Truck", SDL_GetError(), nullptr);
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 800, 450);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SetGameIcon(window);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Excite Truck", SDL_GetError(), window);
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    UiInit(window);

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
                    running = false;
                    break;
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    if (event.window.windowID == SDL_GetWindowID(window)) running = false;
                    break;
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                    UiRefresh();
                    break;
                case SDL_EVENT_GAMEPAD_ADDED:
                    SDL_OpenGamepad(event.gdevice.which);   // read by the menu navigation
                    break;
                case SDL_EVENT_DROP_FILE:
                    if (event.drop.data) UiDropFile(event.drop.data);
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
                    running = false;
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

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
