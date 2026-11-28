// --sweep <list> --sweep-out <dir>: the playtest's picture sweep. Every line of the list is a race set up as
// GT4R_RACE sets one (screen_race.cpp), run in this one window, with pictures at the frames the line asks for; what
// each cost is written to <dir>/results.tsv a line a race.
//
// A line of the list: <name> TAB <GT4R_RACE's setup> [TAB <frames of the pictures, ';' between, default 240>]. '#'
// starts a comment. The pictures are <dir>/<name>_<frame>.png.
//
// results.tsv: name, status (ok, error: the race screen could not be made, left: the race screen closed itself, crash:
// the run ended inside this race), the race screen's setup in ms, the first frame's work in ms, the frames' mean and
// longest work in ms (from the 30th frame), the resident memory in MB after it, how many warnings it logged, the
// warnings (the first 600 characters). A run started again goes on after the last race it finished; a race it was inside when it ended is
// written as a crash and passed over.

#include "gt4r/app/app.h"

#include "base/file.h"
#include "base/log.h"
#include "base/config.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace gt4r {

namespace {

struct Item {
    std::string name, setup;
    std::vector<int> shots;
};

// The warnings logged during the current race (the log's output function is passed through).
struct LogTap {
    SDL_LogOutputFunction previous = nullptr;
    void* previous_data = nullptr;
    int warnings = 0;
    std::string first;
};
LogTap* g_tap = nullptr;

void SDLCALL tap_log(void* data, int category, SDL_LogPriority priority, const char* message) {
    if (g_tap && priority >= SDL_LOG_PRIORITY_WARN) {
        ++g_tap->warnings;
        if (g_tap->first.size() < 600) g_tap->first += (g_tap->first.empty() ? "" : " | ") + std::string(message);
    }
    if (g_tap && g_tap->previous) g_tap->previous(g_tap->previous_data, category, priority, message);
    (void)data;
}

std::string clean(std::string s) {
    for (char& c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

class SweepScreen final : public Screen {
public:
    SweepScreen(std::string list, std::string out) : out_(std::move(out)) {
        if (!out_.empty() && out_.back() != '/' && out_.back() != '\\') out_ += '/';
        SDL_CreateDirectory(out_.c_str());
        if (const std::optional<Bytes> b = gt3r::read_host_file(list)) {
            for (std::string_view line : gt3r::split(std::string_view(reinterpret_cast<const char*>(b->data()), b->size()), '\n')) {
                if (const size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
                const std::vector<std::string_view> f = gt3r::split(line, '\t');
                if (f.size() < 2 || gt3r::trim(f[0]).empty()) continue;
                Item it{std::string(gt3r::trim(f[0])), std::string(gt3r::trim(f[1])), {}};
                if (f.size() > 2)
                    for (std::string_view s : gt3r::split(f[2], ';'))
                        if (!gt3r::trim(s).empty()) it.shots.push_back(std::max(1, std::atoi(std::string(gt3r::trim(s)).c_str())));
                if (it.shots.empty()) it.shots.push_back(240);
                std::sort(it.shots.begin(), it.shots.end());
                items_.push_back(std::move(it));
            }
        } else {
            LOG_ERROR("--sweep: cannot read %s", list.c_str());
        }
        // A run started again: what is done, and the race the last run ended inside.
        std::set<std::string> done;
        if (const std::optional<Bytes> r = gt3r::read_host_file(out_ + "results.tsv"))
            for (std::string_view line : gt3r::split(std::string_view(reinterpret_cast<const char*>(r->data()), r->size()), '\n'))
                if (const size_t tab = line.find('\t'); tab != std::string_view::npos) done.insert(std::string(line.substr(0, tab)));
        std::string last_started;
        if (const std::optional<Bytes> p = gt3r::read_host_file(out_ + "progress.txt"))
            for (std::string_view line : gt3r::split(std::string_view(reinterpret_cast<const char*>(p->data()), p->size()), '\n'))
                if (line.rfind("start ", 0) == 0) last_started = std::string(gt3r::trim(line.substr(6)));
        if (!last_started.empty() && !done.count(last_started)) {
            write_result(last_started, "crash", 0, 0, 0, 0, 0, 0, "the run ended inside this race");
            done.insert(last_started);
        }
        std::erase_if(items_, [&](const Item& it) { return done.count(it.name) > 0; });
        LOG_INFO("--sweep: %zu races to run", items_.size());
        tap_.previous = nullptr;
        SDL_GetLogOutputFunction(&tap_.previous, &tap_.previous_data);
        g_tap = &tap_;
        SDL_SetLogOutputFunction(tap_log, nullptr);
    }
    ~SweepScreen() override {
        child_.reset();
        g_tap = nullptr;
        if (tap_.previous) SDL_SetLogOutputFunction(tap_.previous, tap_.previous_data);
    }

    void update(App& app, double dt) override {
        if (!child_) {
            if (next_ >= items_.size()) { // (the list done: the run ends)
                app.quit();
                return;
            }
            begin(app);
            return;
        }
        const Uint64 t0 = SDL_GetTicksNS();
        child_->update(app, dt);
        frame_work_ = double(SDL_GetTicksNS() - t0) / 1e6;
        if (app.take_pop()) left_ = true; // (the race screen closed itself: the sweep goes on)
    }

    void render(App& app, Ui& ui) override {
        if (!child_) return;
        const Uint64 t0 = SDL_GetTicksNS();
        child_->render(app, ui);
        frame_work_ += double(SDL_GetTicksNS() - t0) / 1e6;
        ++frame_;
        if (frame_ == 1) first_ms_ = frame_work_;
        else if (frame_ >= 30) sum_ms_ += frame_work_, max_ms_ = std::max(max_ms_, frame_work_), ++counted_;
        const Item& it = items_[next_];
        if (shot_ < it.shots.size() && frame_ >= it.shots[shot_]) app.save_picture(out_ + it.name + "_" + std::to_string(it.shots[shot_++]) + ".png");
        if (shot_ >= it.shots.size() || left_) end(app);
    }

private:
    void begin(App& app) {
        const Item& it = items_[next_];
        if (std::FILE* f = std::fopen((out_ + "progress.txt").c_str(), "a")) {
            std::fprintf(f, "start %s\n", it.name.c_str());
            std::fclose(f);
        }
        tap_.warnings = 0, tap_.first.clear();
        frame_ = 0, shot_ = 0, left_ = false, first_ms_ = sum_ms_ = max_ms_ = 0, counted_ = 0;
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "GT4R_RACE", it.setup.c_str(), true);
        const Uint64 t0 = SDL_GetTicksNS();
        try {
            child_ = make_race_screen(app);
        } catch (const std::exception& e) {
            LOG_WARN("--sweep: %s: %s", it.name.c_str(), e.what());
        }
        setup_ms_ = double(SDL_GetTicksNS() - t0) / 1e6;
        if (!child_) {
            write_result(it.name, "error", setup_ms_, 0, 0, 0, App::resident_mb(), tap_.warnings, tap_.first);
            ++next_;
        }
    }

    void end(App&) {
        const Item& it = items_[next_];
        // (A race screen that did not start its race shows its lists and says why in the warnings: "Drive: <why>".)
        const char* status = left_ ? "left" : "ok";
        child_.reset();
        write_result(it.name, status, setup_ms_, first_ms_, counted_ ? sum_ms_ / counted_ : 0, max_ms_, App::resident_mb(), tap_.warnings, tap_.first);
        ++next_;
    }

    void write_result(const std::string& name, const char* status, double setup_ms, double first_ms, double mean_ms, double max_ms, double rss_mb,
                      int warnings, const std::string& first) {
        if (std::FILE* f = std::fopen((out_ + "results.tsv").c_str(), "a")) {
            std::fprintf(f, "%s\t%s\t%.0f\t%.1f\t%.2f\t%.1f\t%.0f\t%d\t%s\n", name.c_str(), status, setup_ms, first_ms, mean_ms, max_ms, rss_mb, warnings,
                         clean(first).c_str());
            std::fclose(f);
        }
    }

    std::string out_;
    std::vector<Item> items_;
    size_t next_ = 0;
    std::unique_ptr<Screen> child_;
    LogTap tap_;
    int frame_ = 0, counted_ = 0;
    size_t shot_ = 0;
    bool left_ = false;
    double setup_ms_ = 0, first_ms_ = 0, sum_ms_ = 0, max_ms_ = 0, frame_work_ = 0;
};

} // namespace

std::unique_ptr<Screen> make_sweep_screen(const std::string& list, const std::string& out) { return std::make_unique<SweepScreen>(list, out); }

} // namespace gt4r
