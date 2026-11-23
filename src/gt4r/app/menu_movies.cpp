#include "gt4r/app/menu_movies.h"

#include "base/log.h"
#include "video/ipu.h"
#include "video/yuv.h"
#include "vfs/vfs.h"

#include <cmath>

namespace gt4r {

MenuMovies::~MenuMovies() {
    stop_all();
    for (auto& [name, t] : still_textures_)
        if (t) SDL_DestroyTexture(t);
}

void MenuMovies::stop_all() { movies_.clear(); }

void MenuMovies::begin_frame() {
    for (auto& [face, m] : movies_) m->seen = false;
}

void MenuMovies::end_frame() {
    for (auto it = movies_.begin(); it != movies_.end();)
        if (!it->second->seen || it->second->face.expired()) it = movies_.erase(it);
        else ++it;
}

SDL_Texture* MenuMovies::still(const std::string& name) {
    if (!stills_read_) {
        stills_read_ = true;
        if (std::optional<Bytes> file = vfs_.read(menu::movie_stills_path(root_))) {
            try {
                for (const gt3r::video::IpuArchiveEntry& e : gt3r::video::parse_ipua(*file)) still_at_[e.name] = {e.offset, e.size};
                stills_ = std::move(*file);
            } catch (const std::exception& e) {
                LOG_WARN("Movie stills: %s", e.what());
            }
        }
    }
    auto found = still_textures_.find(name);
    if (found != still_textures_.end()) return found->second;
    SDL_Texture*& t = still_textures_[name];
    const auto at = still_at_.find(name);
    if (at == still_at_.end() || size_t(at->second.first) + at->second.second > stills_.size()) return t;
    try {
        const gt3r::video::Frame frame = gt3r::video::decode_ipum(ByteSpan(stills_.data() + at->second.first, at->second.second));
        t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STATIC, frame.width, frame.height);
        if (t) SDL_UpdateYUVTexture(t, nullptr, frame.y.data(), frame.width, frame.u.data(), frame.chroma_width(), frame.v.data(), frame.chroma_width());
    } catch (const std::exception& e) {
        LOG_WARN("Movie still %s: %s", name.c_str(), e.what());
    }
    return t;
}

void MenuMovies::draw(const menu::LiveWidget& face, const SDL_FRect& dst, float opacity) {
    const menu::MovieFaceSettings s = menu::movie_face_settings(face.data);
    if (s.movie_name.empty()) return;
    std::unique_ptr<Playing>& m = movies_[&face];
    // A new face, or the scripts named another movie: it starts from its beginning.
    if (!m || m->name != s.movie_name || m->face.expired()) {
        m = std::make_unique<Playing>();
        m->face = const_cast<menu::LiveWidget&>(face).weak_from_this();
        m->name = s.movie_name;
        const std::string path = menu::movie_path(root_, s.movie_name);
        if (!vfs_.exists(path)) {
            LOG_WARN("Menu movie %s: not on the disc", path.c_str());
            m->failed = true;
        } else {
            try {
                m->player.set_audio_device(device_);
                const float volume = s.audio ? 0.8f : 0.0f;
                if (s.repeat) m->player.start_loop([this, path] { return gt3r::video::open_movie(vfs_, path); }, volume);
                else m->player.start(gt3r::video::open_movie(vfs_, path), volume);
            } catch (const std::exception& e) {
                LOG_WARN("Menu movie %s: %s", path.c_str(), e.what());
                m->failed = true;
            }
        }
    }
    m->seen = true;
    const bool frozen = face.data.number("un_update") != 0;
    if (!m->failed && m->player.paused() != frozen) m->player.set_paused(frozen);
    if (!m->failed) m->player.update(r_);

    const u8 alpha = u8(std::clamp(opacity, 0.0f, 1.0f) * 255.0f);
    if (SDL_Texture* t = m->failed ? nullptr : m->player.texture()) {
        SDL_SetTextureAlphaMod(t, alpha);
        SDL_SetTextureBlendMode(t, alpha < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
        if (s.interlace && !m->player.field_split()) {
            // An interlaced movie holds its two fields as the frame's halves (`interlace`, the
            // program's flag): until the player has found that from the pictures (flat ones such
            // as the opening's white logo page do not tell), the first field.
            float w = 0, h = 0;
            SDL_GetTextureSize(t, &w, &h);
            const SDL_FRect src{0, 0, w, h / 2};
            SDL_RenderTexture(r_, t, &src, &dst);
        } else m->player.draw(r_, dst);
    } else if (SDL_Texture* still_t = still(s.movie_name)) {
        // Until the movie's first picture: its still, as the program has it for the name.
        SDL_SetTextureAlphaMod(still_t, alpha);
        SDL_RenderTexture(r_, still_t, nullptr, &dst);
    }
    if (m->failed) {
        if (!s.repeat && !m->finish_told && on_event) {
            m->finish_told = true;
            on_event(const_cast<menu::LiveWidget&>(face), "onMovieFinish");
        }
        return;
    }
    const double rate = m->player.info().frame_rate;
    const long frame = rate > 0 && m->player.texture() ? long(std::lround(m->player.picture_time() * rate)) : -1;
    const long hook = face.data.has("frame_hook") ? long(face.data.number("frame_hook", -1)) : -1;
    // Ended: the player says so, or no new picture came for a second while its clock ran (the
    // opening's sound does not drain at its end, so the player's own test never passes).
    const bool ended = m->player.finished() || (m->player.texture() && !m->player.paused() && m->player.position() - m->player.picture_time() > 1.0);
    const menu::MovieFaceEvents e = menu::movie_face_events(s, frame, hook, ended, m->finish_told);
    menu::LiveWidget& live = const_cast<menu::LiveWidget&>(face);
    if (e.hook) {
        const std::string name = face.data.text("frame_hook_event");
        live.set_property("frame_hook", script::Value::integer(-1));
        if (on_event && !name.empty()) on_event(live, name);
    }
    if (e.finished) {
        m->finish_told = true;
        if (on_event) on_event(live, "onMovieFinish");
    }
}

} // namespace gt4r
