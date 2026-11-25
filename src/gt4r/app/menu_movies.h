#pragma once

// The movie faces of the running menus (MMovieFace): each shown face plays its movie with the
// engine's player, drawn in its place in the page; a movie that does not repeat calls the
// face's onMovieFinish when it ends. docs/formats/menu.md, "Movies".

#include "gt4r/menu/faces_movie.h"
#include "gt4r/menu/runtime.h"
#include "video/movie_player.h"

#include <SDL3/SDL_render.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace gt3r {
class Vfs;
}

namespace gt4r {

class MenuMovies {
public:
    MenuMovies(SDL_Renderer* renderer, gt3r::Vfs& vfs, SDL_AudioDeviceID device, float volume = 0.8f) : r_(renderer), vfs_(vfs), device_(device), volume_(volume) {}
    ~MenuMovies();

    // The configuration's MpegRoot ("main"); "" : "preview".
    void set_root(const std::string& root) { root_ = root; }
    // Called with the face and the name of its function to call (onMovieFinish, a frame hook's).
    std::function<void(menu::LiveWidget& face, const std::string& function)> on_event;

    // Before the pages are drawn / after: the movies of faces not drawn in between are stopped.
    void begin_frame();
    void end_frame();
    // Draws a movie face (PageDrawer::draw_live).
    void draw(const menu::LiveWidget& face, const SDL_FRect& dst, float opacity);
    void stop_all();
    size_t playing() const { return movies_.size(); }

private:
    struct Playing {
        std::weak_ptr<script::Heap> face;
        std::string name;
        gt3r::video::MoviePlayer player;
        bool finish_told = false, seen = false, failed = false;
    };
    // The still for a movie from course.ipic while its first picture is decoded (nullptr: none).
    SDL_Texture* still(const std::string& name);

    SDL_Renderer* r_;
    gt3r::Vfs& vfs_;
    SDL_AudioDeviceID device_;
    float volume_; // the app's music volume
    std::string root_;
    std::map<const menu::LiveWidget*, std::unique_ptr<Playing>> movies_;
    bool stills_read_ = false;
    Bytes stills_;
    std::map<std::string, std::pair<u32, u32>> still_at_; // name -> offset, size
    std::map<std::string, SDL_Texture*> still_textures_;
};

} // namespace gt4r
