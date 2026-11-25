#pragma once

// Saving the game's textures (Settings > Mods), on a thread of its own: every file of the disc whose textures a pack can replace
// (settings/texture_files.h). The mods must not change meanwhile; the page waits.

#include "gt4r/app/app.h"
#include "gt4r/settings/texture_files.h"

#include "base/log.h"
#include "formats/texture_pack.h"
#include "gt4r/disc/disc.h"

#include <atomic>
#include <string>
#include <thread>

namespace gt4r {

class TextureDump {
public:
    ~TextureDump() {
        cancel_ = true;
        if (thread_.joinable()) thread_.join();
    }
    void start(App& app, std::string dir) {
        if (thread_.joinable() || !app.vfs() || !app.disc()) return;
        done_ = cancel_ = false, saved_ = 0, progress_ = 0;
        Disc* disc = app.disc();
        thread_ = std::thread([this, disc, dir = std::move(dir)] {
            run(*disc, dir);
            done_.store(true, std::memory_order_release);
        });
    }
    bool running() const { return thread_.joinable(); }
    bool finished() const { return thread_.joinable() && done_.load(std::memory_order_acquire); }
    void cancel() { cancel_ = true; }
    void close(App&) { thread_.join(); }
    float progress() const { return progress_; }
    size_t saved() const { return saved_; }
    const std::string& message() const { return message_; } // once finished
    bool ok() const { return ok_; }

private:
    void run(Disc& disc, const std::string& dir) {
        // (the disc's own files, not the mods': a pack replaces the game's textures)
        gt3r::Volume& volume = disc.volume();
        const settings::FileReader read = [&volume](const std::string& path) -> std::optional<Bytes> {
            if (const auto* e = volume.find(path)) return volume.read(*e);
            return std::nullopt;
        };
        gt3r::texture_pack::Dump dump(dir);
        bool written = true;
        const std::vector<ImageFile>& files = disc.files();
        for (size_t i = 0; i < files.size() && !cancel_; ++i) {
            progress_ = float(double(i) / double(files.size()));
            const std::string& path = files[i].path;
            if (!settings::has_pack_textures(path)) continue;
            try {
                if (const std::optional<Bytes> data = read(path)) settings::dump_file(dump, path, *data, read, &written);
            } catch (const std::exception& e) {
                LOG_WARN("Textures: %s: %s", path.c_str(), e.what());
            }
            saved_ = dump.saved();
        }
        written = dump.finish() && written;
        ok_ = written;
        message_ = (cancel_ ? "Stopped. " : "") + std::to_string(dump.saved()) + " textures saved in " + dir + (written ? "" : " (some could not be written)");
    }

    std::thread thread_;
    std::atomic<bool> done_{false}, cancel_{false};
    std::atomic<float> progress_{0};
    std::atomic<size_t> saved_{0};
    std::string message_;
    bool ok_ = false;
};

} // namespace gt4r
