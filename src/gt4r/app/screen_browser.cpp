// Asset browser: the disc's files (with the mods over them) by folder, with a preview of what
// the engine already decodes: textures (alone or inside any other file), music and voice
// streams, movies; everything else shows its size, kind and first bytes.

#include "gt4r/app/app.h"
#include "gt4r/app/page.h"

#include "base/log.h"
#include "formats/gs_memory.h"
#include "formats/tex1.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/formats/movie_tracks.h"
#include "video/movie_player.h"

#include <algorithm>

namespace gt4r {

namespace {

constexpr size_t kMaxTextures = 400;      // per file (a course holds thousands)
constexpr u32 kMaxPreviewBytes = 96u << 20;

struct Preview {
    std::string path;
    std::string summary;
    std::vector<SDL_Texture*> textures;
    std::vector<std::string> labels;
    Bytes head; // first bytes for a hex view
    int current = 0;

    void clear() {
        for (SDL_Texture* t : textures) SDL_DestroyTexture(t);
        textures.clear();
        labels.clear();
        head.clear();
        summary.clear();
        path.clear();
        current = 0;
    }
};

bool ends_with(const std::string& s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string size_text(u64 bytes) {
    char b[32];
    if (bytes >= 10u << 20) std::snprintf(b, sizeof b, "%.1f MB", double(bytes) / (1 << 20));
    else if (bytes >= 10u << 10) std::snprintf(b, sizeof b, "%.1f KB", double(bytes) / (1 << 10));
    else std::snprintf(b, sizeof b, "%llu bytes", static_cast<unsigned long long>(bytes));
    return b;
}

std::string magic_text(ByteSpan d) {
    if (d.size() < 4) return {};
    for (size_t i = 0; i < 4; ++i)
        if (d[i] < 0x20 || d[i] > 0x7E) return {};
    return std::string(reinterpret_cast<const char*>(d.data()), 4);
}

class BrowserScreen : public Screen {
public:
    explicit BrowserScreen(std::string open_path) : open_path_(std::move(open_path)) {}
    ~BrowserScreen() override {
        movie_.stop();
        preview_.clear();
    }

    void update(App& app, double) override {
        if (entries_dirty_) refresh(app);
        if (!open_path_.empty()) { // jump straight to a file (--browse)
            const std::string path = gt3r::normalize_vfs_path(open_path_);
            open_path_.clear();
            dir_ = path.substr(0, path.find_last_of('/') + 1);
            refresh(app);
            const auto it = std::find(entries_.begin(), entries_.end(), path.substr(dir_.size()));
            if (it != entries_.end()) {
                sel_ = int(it - entries_.begin());
                if (it->back() != '/') open(app, path);
            }
        }
        if (movie_.active()) {
            movie_.update(app.renderer());
            if (movie_.finished()) movie_.stop();
        }
        const MenuInput& m = app.menu();
        const int n = int(entries_.size());
        if (n > 0) {
            const int page = m.pressed(PadButton::L1) ? -kRows : m.pressed(PadButton::R1) ? kRows : 0;
            sel_ = std::clamp(sel_ + m.vertical() + page, 0, n - 1);
        }
        const bool clicked = mouse_.pick(app, 0, sel_, n);
        if (!preview_.textures.empty() && m.horizontal())
            preview_.current = (preview_.current + m.horizontal() + int(preview_.textures.size())) % int(preview_.textures.size());
        if (m.pressed(PadButton::Triangle) && dir_.empty()) {
            app.audio().stop_music();
            app.pop();
            return;
        }
        if (m.pressed(PadButton::Triangle)) {
            const std::string prev = dir_;
            dir_.pop_back();
            dir_ = dir_.substr(0, dir_.find_last_of('/') + 1);
            refresh(app);
            const auto it = std::find(entries_.begin(), entries_.end(), prev.substr(dir_.size()));
            sel_ = it == entries_.end() ? 0 : int(it - entries_.begin());
            return;
        }
        if ((m.pressed(PadButton::Cross) || clicked) && n > 0) {
            const std::string& e = entries_[size_t(sel_)];
            if (e.back() == '/') {
                dir_ += e;
                sel_ = 0;
                refresh(app);
            } else {
                open(app, dir_ + e);
            }
        }
        if (m.pressed(PadButton::Circle)) {
            app.audio().stop_music();
            if (audio::AppSound* sound = app.sound()) sound->stop();
            movie_.stop();
        }
        if (m.pressed(PadButton::Select)) app.show_open_dialog();
        if (m.pressed(PadButton::Square) && !models_.empty()) view_models(app);
    }

    void render(App& app, Ui& ui) override {
        ui.header("ASSET BROWSER", app.disc()->info().description + "   /" + dir_);
        const int n = int(entries_.size());
        const int first = ui.list_window(n, sel_, kRows), over = mouse_.over(app, 0);
        mouse_.begin(), mouse_.rows(0, first, std::min(kRows, n - first), 4, 24, 250, 12);
        for (int i = 0; i < kRows && first + i < n; ++i) {
            const int idx = first + i;
            const float y = 26 + i * 12.0f;
            if (idx == sel_ || idx == over) ui.fill(4, y - 2, 250, 12, idx == sel_ ? colors::kHighlight : colors::kPanel);
            const std::string& name = entries_[size_t(idx)];
            const bool is_dir = name.back() == '/';
            const std::string src = is_dir ? "" : app.vfs()->source_of(dir_ + name);
            const Color c = is_dir ? colors::kAccent : (src != "disc" ? colors::kGood : colors::kText);
            ui.text(8, y, Ui::elide(name, 236), c);
        }
        if (n > kRows) ui.text_right(250, Ui::kHeight - 30, std::to_string(sel_ + 1) + "/" + std::to_string(n), colors::kDim);

        // Preview pane.
        const float px = 262, py = 26, pw = Ui::kWidth - px - 6, ph = Ui::kHeight - 50 - py;
        ui.fill(px, py, pw, ph, colors::kPanel);
        if (!preview_.path.empty()) {
            ui.text(px + 6, py + 6, Ui::elide(preview_.path, pw - 12), colors::kAccent);
            ui.text(px + 6, py + 18, Ui::elide(preview_.summary, pw - 12), colors::kDim);
        } else {
            const DiscInfo& d = app.disc()->info();
            ui.text(px + 6, py + 6, d.description, colors::kAccent);
            ui.text(px + 6, py + 18, d.serial + "  version " + d.version + "  " + d.video, colors::kDim);
            float y = py + 36;
            for (const VolumeInfo& v : app.disc()->volumes())
                ui.text(px + 6, y, v.name + ": " + std::to_string(v.files) + " files, " + size_text(v.size), colors::kText), y += 12;
            if (!d.verified) ui.text(px + 6, y + 6, d.modified ? "A modified copy of this release." : "This release has not been tested.", colors::kBad);
        }
        if (movie_.active() && movie_.texture()) {
            movie_.draw(ui.renderer(), movie_.fit_4_3(px + 6, py + 32, pw - 12, ph - 52));
        } else if (!preview_.textures.empty()) {
            SDL_Texture* t = preview_.textures[size_t(preview_.current)];
            float tw, th;
            SDL_GetTextureSize(t, &tw, &th);
            const float box_w = pw - 12, box_h = ph - 52;
            const float s = std::min({box_w / tw, box_h / th, 2.0f});
            const SDL_FRect dst{px + 6 + (box_w - tw * s) / 2, py + 32 + (box_h - th * s) / 2, tw * s, th * s};
            draw_checker(ui, dst);
            SDL_RenderTexture(ui.renderer(), t, nullptr, &dst);
            ui.text(px + 6, py + ph - 14, preview_.labels[size_t(preview_.current)], colors::kText);
        } else if (!preview_.head.empty()) {
            for (size_t row = 0; row < 16 && row * 8 < preview_.head.size(); ++row) {
                char line[80];
                int o = std::snprintf(line, sizeof(line), "%04zx ", row * 8);
                for (size_t i = 0; i < 8 && row * 8 + i < preview_.head.size(); ++i)
                    o += std::snprintf(line + o, sizeof(line) - size_t(o), "%02x ", preview_.head[row * 8 + i]);
                for (size_t i = 0; i < 8 && row * 8 + i < preview_.head.size(); ++i) {
                    const u8 c = preview_.head[row * 8 + i];
                    line[o++] = (c >= 32 && c < 127) ? char(c) : '.';
                }
                line[o] = 0;
                ui.text_mono(px + 6, py + 36 + float(row) * 11.0f, line, colors::kText);
            }
        }
        if (app.audio().music_playing()) {
            const double pos = app.audio().music_position(), len = app.audio().music_length();
            ui.bar(px + 6, Ui::kHeight - 40, pw - 12, 6, len > 0 ? float(pos / len) : 0.0f, colors::kAccent);
        }
        ui.footer(models_.empty() ? "{Cross}: open  {Triangle}: up  {L1}/{R1}: page  {Left/Right}: texture  {Circle}: stop  {Select}: another disc"
                                  : "{Cross}: open  {Square}: view models  {Triangle}: up  {L1}/{R1}: page  {Left/Right}: texture  {Circle}: stop");
    }

private:
    static constexpr int kRows = 25;

    void refresh(App& app) {
        entries_ = app.vfs()->list_dir(dir_);
        entries_dirty_ = false;
        sel_ = std::clamp(sel_, 0, std::max(0, int(entries_.size()) - 1));
    }

    void draw_checker(Ui& ui, const SDL_FRect& r) {
        const float cell = 8;
        for (float y = 0; y < r.h; y += cell)
            for (float x = 0; x < r.w; x += cell) {
                const bool odd = (int(x / cell) + int(y / cell)) & 1;
                ui.fill(r.x + x, r.y + y, std::min(cell, r.w - x), std::min(cell, r.h - y), odd ? Color{60, 60, 66} : Color{44, 44, 50});
            }
    }

    // "1.2 MB, packed (0.4 MB on the disc), GT4.VOL" or "..., from the mod <id>".
    std::string where(App& app, const std::string& path, u64 size) {
        std::string s = size_text(size);
        const std::string src = app.vfs()->source_of(path);
        if (src != "disc") return s + ", from the mod " + src;
        if (const ImageFile* f = app.disc()->file(path)) {
            if (f->packed) s += ", packed (" + size_text(f->stored_size) + " on the disc)";
            s += ", " + app.disc()->volumes()[f->volume].name;
        }
        return s;
    }

    // The 3D viewer on the open file's model sets (a course: its scenery as a whole first).
    void view_models(App& app) {
        const std::string path = preview_.path;
        if (course_) {
            movie_.stop();
            app.push(make_model_viewer(path, {"Scenery", "Scenery, backdrop and sky", "Reflection scenery", "Backdrop", "Sky", "Road, walls and start grid",
                                        "Scenery with the road over it"},
                                       [path](App& a, size_t i) {
                                           const std::optional<Bytes> data = a.vfs()->read(path);
                                           if (!data) throw FormatError("Could not read " + path);
                                           course::ModelOptions o;
                                           o.world = i <= 1 || i == 6, o.after = o.world, o.far = i == 1 || i == 3, o.sky = i == 1 || i == 4;
                                           o.environment = i == 2, o.road = i >= 5;
                                           return course::build_course_model(*data, o, path);
                                       }));
            return;
        }
        std::vector<std::string> items;
        for (const auto& [off, h] : models_) {
            char b[96];
            std::snprintf(b, sizeof b, "@%zx  %u models, %u shapes", off, h.models, h.shapes);
            items.push_back(b);
        }
        std::vector<size_t> offsets;
        for (const auto& [off, h] : models_) offsets.push_back(off);
        movie_.stop();
        app.push(make_model_viewer(path, std::move(items), [path, offsets](App& a, size_t i) {
            const std::optional<Bytes> data = a.vfs()->read(path);
            if (!data || offsets[i] >= data->size()) throw FormatError("Could not read " + path);
            mdls::Options o;
            if (path.compare(0, 6, "wheel/") == 0) o.scene.outside_sets[mdls::kWheelFaceSlot] = mdls::kWheelFaceSet;
            return mdls::build_model(ByteSpan(*data).subspan(offsets[i]), o, path);
        }));
    }

    void open(App& app, const std::string& path) {
        movie_.stop();
        preview_.clear();
        models_.clear();
        course_ = false;
        preview_.path = path;
        if (ends_with(path, ".ads")) {
            const auto stream = app.vfs()->open_stream(path);
            const bool ok = app.audio().play_music(*app.vfs(), path, false);
            preview_.summary = where(app, path, stream ? stream->size : 0) + (ok ? ", playing" : ", could not play this stream");
            return;
        }
        if (ends_with(path, ".sqt") || ends_with(path, ".ins")) { // sequenced music and effect banks
            audio::AppSound* sound = app.sound();
            const auto stream = app.vfs()->open_stream(path);
            preview_.summary = where(app, path, stream ? stream->size : 0) + (sound ? sound->preview(path) : std::string());
            return;
        }
        if (ends_with(path, ".pss")) {
            const auto stream = app.vfs()->open_stream(path);
            preview_.summary = where(app, path, stream ? stream->size : 0);
            try {
                app.audio().stop_music();
                movie_.set_audio_device(app.audio().output_device_id());
                movie_.start(gt4r::open_movie(*app.vfs(), path), app.audio().music_volume());
                const gt3r::video::MovieInfo& info = movie_.info();
                char b[96];
                std::snprintf(b, sizeof b, ", movie %dx%d %.2f fps", info.width, info.height, info.frame_rate);
                preview_.summary += b;
            } catch (const std::exception& e) {
                LOG_WARN("Movie %s: %s", path.c_str(), e.what());
                preview_.summary += ", could not play this movie";
            }
            return;
        }
        const auto stream = app.vfs()->open_stream(path);
        if (stream && stream->size > kMaxPreviewBytes) {
            preview_.summary = where(app, path, stream->size) + ", too large to preview";
            return;
        }
        std::optional<Bytes> data;
        try {
            data = app.vfs()->read(path);
        } catch (const std::exception& e) {
            LOG_WARN("%s: %s", path.c_str(), e.what());
        }
        if (!data) {
            preview_.summary = "Could not read this file";
            return;
        }
        preview_.head.assign(data->begin(), data->begin() + std::min<size_t>(data->size(), 128));
        preview_.summary = where(app, path, data->size());
        if (const std::string magic = magic_text(*data); !magic.empty()) preview_.summary += ", " + magic;
        size_t total = 0;
        try {
            for (size_t off : gt3r::find_tex1_sets(*data)) {
                const ByteSpan sub = ByteSpan(*data).subspan(off);
                const gt3r::Tex1 set = gt3r::Tex1::parse(sub);
                total += set.textures.size();
                if (preview_.textures.size() >= kMaxTextures) continue;
                gt3r::GsMemory gs;
                set.upload(sub, gs);
                for (size_t i = 0; i < set.textures.size() && preview_.textures.size() < kMaxTextures; ++i) {
                    const gt3r::Tex1::Image img = set.decode(gs, i);
                    if (img.width == 0 || img.height == 0 || img.width > 2048 || img.height > 2048) continue;
                    SDL_Texture* t = SDL_CreateTexture(app.renderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, int(img.width), int(img.height));
                    if (!t) continue;
                    SDL_UpdateTexture(t, nullptr, img.rgba.data(), int(img.width * 4));
                    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                    preview_.textures.push_back(t);
                    char label[160];
                    std::snprintf(label, sizeof(label), "%zu  set @%zx tex %zu/%zu  %ux%u %s", preview_.textures.size(), off, i + 1,
                                  set.textures.size(), img.width, img.height, gt3r::psm_name(set.textures[i].tex0.psm));
                    preview_.labels.push_back(label);
                }
            }
        } catch (const std::exception& e) {
            LOG_WARN("Texture decode failed for %s: %s", path.c_str(), e.what());
        }
        for (size_t off : mdls::find_model_sets(*data)) {
            try {
                models_.emplace_back(off, mdls::read_header(ByteSpan(*data).subspan(off)));
            } catch (const std::exception&) {
            }
        }
        course_ = course::CourseFile::looks_like_course(*data);
        if (course_) preview_.summary += ", a course";
        else if (!models_.empty()) preview_.summary += ", " + std::to_string(models_.size()) + " model set(s)";
        if (total) {
            preview_.summary += ", " + std::to_string(total) + " texture(s)";
            if (total > preview_.textures.size()) preview_.summary += " (first " + std::to_string(preview_.textures.size()) + " shown)";
        }
    }

    RowPointer mouse_;
    std::string open_path_;
    std::string dir_;
    std::vector<std::string> entries_;
    bool entries_dirty_ = true;
    int sel_ = 0;
    Preview preview_;
    std::vector<std::pair<size_t, mdls::Header>> models_; // the open file's model sets
    bool course_ = false; // the open file is a course
    gt3r::video::MoviePlayer movie_;
};

} // namespace

std::unique_ptr<Screen> make_browser_screen(const std::string& open_path) { return std::make_unique<BrowserScreen>(open_path); }

} // namespace gt4r
