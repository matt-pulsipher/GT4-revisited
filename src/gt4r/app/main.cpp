// Entry point using SDL3's callback model, which maps onto every platform's native
// application lifecycle.

#define SDL_MAIN_USE_CALLBACKS 1
#include "gt4r/app/app.h"

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstring>

using gt4r::App;

SDL_AppResult SDL_AppInit(void** state, int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::printf("usage: gt4r [disc.iso|disc.chd] [options]\n\n"
                        "  --browse <path>     open the asset browser on a file of the disc\n"
                        "  --screen <name>     open cars, courses or menus at start\n"
                        "  --shot <out.png>    save a picture of one frame, then quit\n"
                        "  --shot-frame <n>    which frame --shot saves (default 30)\n"
                        "  --keys <list>       scripted menu presses, e.g. \"down,down,cross,wait60,right\"\n"
                        "                      (\"shot:<name>\": a picture <name>.png beside --shot's)\n"
                        "  --no-gpu            2D screens only\n");
            return SDL_APP_SUCCESS;
        }
    auto* app = new App();
    *state = app;
    return app->init(argc, argv) ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppIterate(void* state) { return static_cast<App*>(state)->iterate(); }

SDL_AppResult SDL_AppEvent(void* state, SDL_Event* event) { return static_cast<App*>(state)->event(*event); }

void SDL_AppQuit(void* state, SDL_AppResult) {
    if (auto* app = static_cast<App*>(state)) {
        app->shutdown();
        delete app;
    }
}
