// Entry point using SDL3's callback model, which maps onto every platform's native
// application lifecycle.

#define SDL_MAIN_USE_CALLBACKS 1
#include "gt4r/app/app.h"
#include "gt3r_version.h"
#include "gt4r_version.h"

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

using gt4r::App;

namespace {

// --help and --version print before SDL starts (no window). On Windows the game has no console of its own: the text goes to
// the one it was started from, if any.
void to_console() {
#ifdef _WIN32
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
    }
#endif
}

} // namespace

SDL_AppResult SDL_AppInit(void** state, int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--version")) {
            to_console();
            std::printf("GT4 Revisited %s (engine %s)\n", GT4R_VERSION, GT3R_VERSION);
            return SDL_APP_SUCCESS;
        }
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            to_console();
            std::printf("GT4 Revisited %s\n\n", GT4R_VERSION);
            std::printf("usage: gt4r [disc.iso|disc.chd] [options]\n\n"
                        "With a disc and no other choice the game starts from power on; Select on the\n"
                        "title (or its GT4R menu button) leaves it for the home screen ([game] start =\n"
                        "hub in config.ini: the home screen first).\n\n"
                        "  --browse <path>     open the asset browser on a file of the disc\n"
                        "  --screen <name>     open game, hub, settings, controls, cars, courses, menus or race at start\n"
                        "  --shot <out.png>    save a picture of one frame, then quit\n"
                        "  --shot-frame <n>    which frame --shot saves (default 30); with --shot a frame\n"
                        "                      is a 60th of a second, held to that pace (--fast: not held;\n"
                        "                      the menus' movies then run behind)\n"
                        "  --keys <list>       scripted presses, e.g. \"down,down,cross,wait60,right\"\n"
                        "                      (\"select+start\": two together;\n"
                        "                      \"shot:<name>\": a picture <name>.png beside --shot's;\n"
                        "                      \"gas=1\", \"brake=0.5\", \"steer=-0.3\", \"reverse=1\",\n"
                        "                      \"handbrake=1\" hold a race control until changed,\n"
                        "                      \"release\" lets all go; \"quit\" ends the run;\n"
                        "                      \"click:<x>:<y>\", \"move:\", \"back:\" (the right button),\n"
                        "                      \"wheel:<turns>:<x>:<y>\", \"drag:<x>:<y>:<x>:<y>\": the mouse,\n"
                        "                      in the page's 640 x 360 units; \"drop:<file>\"; \"f12\";\n"
                        "                      \"type:<text>\": text typed on a keyboard (\"typefile:<file>\":\n"
                        "                      the file's text, read when the key comes);\n"
                        "                      \"enter\": the Enter key;\n"
                        "                      \"@<file>\": the keys from a file, '#' comments)\n"
                        "  --record <out.mkv>  a video of the run through ffmpeg, a 60th of a second a\n"
                        "                      frame, the sound effects in <out>.sfx.wav, no music\n"
                        "                      (--record-frames <n>: so many frames, then quit)\n"
                        "  --stats <file>      a line a second: frame, seconds, mean and longest\n"
                        "                      frame work (ms), resident memory (MB)\n"
                        "  --sweep <list>      run the races of a list one after another, pictures of\n"
                        "                      each (screen_sweep.cpp); --sweep-out <dir>: where\n"
                        "  --visible           a scripted run (--shot, --keys, --sweep, --stats, GT4R_RACE,\n"
                        "                      GT4R_MENU, GT4R_VIEWER) with its window; without it nothing\n"
                        "                      is shown or heard and no real input read (GT4R_VISIBLE=1)\n"
                        "  --headless          any run without a window (GT4R_HEADLESS=1)\n"
                        "  --pace <n>          frames a second of a run without a window that goes by\n"
                        "                      the clock (default 60; display: its rate; off: not held)\n"
                        "  --no-gpu            2D screens only\n"
                        "  --version           the version, then quit\n");
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
