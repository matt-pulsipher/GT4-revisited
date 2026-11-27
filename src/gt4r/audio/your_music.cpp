#include "gt4r/audio/your_music.h"

#include "audio/audio_engine.h"
#include "base/file.h"
#include "base/frame_clock.h"
#include "base/log.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <unordered_map>

namespace gt4r::audio {

namespace {

bool parse_hex(std::string_view text, u64& out) {
    if (text.empty() || text.size() > 16) return false;
    u64 v = 0;
    for (char c : text) {
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return false;
        v = v << 4 | u64(d);
    }
    out = v;
    return true;
}

} // namespace

std::string RaceSong::text() const {
    char buf[40];
    if (piece >= 0) std::snprintf(buf, sizeof buf, "disc:%d", piece);
    else if (id) std::snprintf(buf, sizeof buf, "yours:%" PRIx64, id);
    else return {};
    return buf;
}

RaceSong RaceSong::parse(std::string_view text) {
    RaceSong s;
    if (text.substr(0, 5) == "disc:") {
        const std::string n(text.substr(5));
        char* end = nullptr;
        const long v = std::strtol(n.c_str(), &end, 10);
        if (!n.empty() && end == n.c_str() + n.size() && v >= 0 && v < 4096) s.piece = int(v);
    } else if (text.substr(0, 6) == "yours:") {
        u64 id = 0;
        if (parse_hex(text.substr(6), id)) s.id = id;
    }
    return s;
}

SongSource parse_song_source(std::string_view s) { return s == "game" ? SongSource::Game : s == "both" ? SongSource::Both : SongSource::Yours; }

std::string_view song_source_name(SongSource s) { return s == SongSource::Game ? "game" : s == SongSource::Both ? "both" : "yours"; }

gt3r::race::MusicLibrary::StreamOpener disc_piece_opener(gt3r::Vfs& vfs, std::string path) {
    return [vfs = &vfs, path = std::move(path)]() -> std::unique_ptr<gt3r::MusicStream> {
        std::optional<Bytes> b = vfs->read(path);
        if (!b) return nullptr;
        auto data = std::make_shared<const Bytes>(std::move(*b));
        return gt3r::open_ads_music([data](u64 offset, void* out, size_t len) {
            if (offset + len > data->size()) throw gt3r::FormatError("read past the end of the piece");
            std::memcpy(out, data->data() + offset, len);
        });
    };
}

size_t songs_left(const RacePlaylist& list) {
    return size_t(std::count_if(list.rows().begin(), list.rows().end(), [](const RacePlaylist::Row& r) { return r.enabled && !r.played; }));
}

bool has_songs(const RacePlaylist& list) {
    return std::any_of(list.rows().begin(), list.rows().end(), [](const RacePlaylist::Row& r) { return r.enabled; });
}

// ---- The player's list

int YourPlaylist::row_of(u64 id) const {
    const auto it = std::find(ids_.begin(), ids_.end(), id);
    return it == ids_.end() ? -1 : int(it - ids_.begin());
}

void YourPlaylist::rebuild() { list_.build(); }

bool YourPlaylist::sync(const std::vector<u64>& ids) {
    if (ids == ids_) return false;
    std::unordered_map<u64, bool> was;
    for (size_t i = 0; i < ids_.size(); ++i) was.emplace(ids_[i], list_.rows()[i].enabled);
    RacePlaylist next(ids.size());
    next.set_shuffle(list_.shuffle(), list_.seed());
    for (size_t i = 0; i < ids.size(); ++i) {
        const auto it = was.find(ids[i]);
        next.rows()[i].enabled = it == was.end() || it->second;
    }
    list_ = std::move(next);
    ids_ = ids;
    rebuild();
    return true;
}

bool YourPlaylist::set_shuffle(bool shuffle) {
    if (shuffle == list_.shuffle()) return false;
    list_.set_shuffle(shuffle, list_.seed() ? list_.seed() : u16(gt3r::frame_clock::random_seed() | 1));
    rebuild();
    return true;
}

bool YourPlaylist::set_checked(u64 id, bool on) {
    const int r = row_of(id);
    if (r < 0 || list_.rows()[size_t(r)].enabled == on) return false;
    list_.rows()[size_t(r)].enabled = on;
    rebuild();
    return true;
}

bool YourPlaylist::checked(u64 id) const {
    const int r = row_of(id);
    return r >= 0 && list_.rows()[size_t(r)].enabled;
}

size_t YourPlaylist::checked_count() const {
    return size_t(std::count_if(list_.rows().begin(), list_.rows().end(), [](const RacePlaylist::Row& r) { return r.enabled; }));
}

// The file: a header line, then a line per song: its id (hex), checked, its place (-1: none), played this round.
std::string YourPlaylist::to_text() const {
    std::string out;
    char line[96];
    std::snprintf(line, sizeof line, "gt4r-playlist 1 position %d shuffle %d seed %u\n", list_.position(), list_.shuffle() ? 1 : 0, unsigned(list_.seed()));
    out += line;
    for (size_t i = 0; i < ids_.size(); ++i) {
        const RacePlaylist::Row& r = list_.rows()[i];
        std::snprintf(line, sizeof line, "%016" PRIx64 " %d %d %d\n", ids_[i], r.enabled ? 1 : 0, r.order, r.played ? 1 : 0);
        out += line;
    }
    return out;
}

YourPlaylist YourPlaylist::parse(std::string_view text) {
    YourPlaylist p;
    std::istringstream in{std::string(text)};
    std::string head, word;
    int version = 0, position = 0, shuffle = 0;
    unsigned seed = 0;
    if (!(in >> head >> version) || head != "gt4r-playlist" || version != 1) return p;
    if (!(in >> word >> position) || word != "position" || !(in >> word >> shuffle) || word != "shuffle" || !(in >> word >> seed) || word != "seed") return p;
    std::vector<u64> ids;
    std::vector<RacePlaylist::Row> rows;
    std::string id;
    int enabled = 0, order = -1, played = 0;
    while (in >> id >> enabled >> order >> played) {
        u64 v = 0;
        if (!parse_hex(id, v)) break;
        ids.push_back(v);
        RacePlaylist::Row r;
        r.enabled = enabled != 0, r.order = std::clamp(order, -1, 1 << 20), r.played = played != 0;
        rows.push_back(r);
    }
    p.list_ = RacePlaylist(rows.size());
    p.list_.rows() = std::move(rows);
    p.list_.set_position(std::max(0, position));
    p.list_.set_shuffle(shuffle != 0, u16(seed));
    p.ids_ = std::move(ids);
    // (places that no longer make an order, a file edited by hand: made again)
    std::vector<int> places;
    for (const RacePlaylist::Row& r : p.list_.rows())
        if (r.enabled) places.push_back(r.order);
    std::sort(places.begin(), places.end());
    for (size_t i = 0; i < places.size(); ++i)
        if (places[i] != int(i)) {
            p.rebuild();
            break;
        }
    return p;
}

// ---- The player's music

YourMusic::YourMusic() : rng_(gt3r::frame_clock::random_seed()) {}

YourMusic::~YourMusic() { library_.cancel(); }

void YourMusic::set_files(const std::string& data_dir) {
    if (data_dir.empty()) return;
    playlist_file_ = data_dir + "music_playlist.txt";
    library_.set_song_list_file(data_dir + "music_songs.txt");
}

void YourMusic::set_folder(const std::string& folder) { library_.set_folder(folder); }

bool YourMusic::poll() {
    const bool changed = library_.poll();
    if (!changed && playlist_read_) return false;
    if (!playlist_read_) {
        playlist_read_ = true;
        if (!playlist_file_.empty())
            if (const std::optional<Bytes> b = gt3r::read_host_file(playlist_file_)) playlist_ = YourPlaylist::parse(std::string(b->begin(), b->end()));
    }
    // A folder that can't be read (a drive unplugged) leaves the list as it is, for when it is back.
    if (!library_.folder_found() && !library_.has_songs()) return changed;
    std::vector<u64> ids;
    ids.reserve(library_.songs().size());
    for (const gt3r::race::CustomSong& s : library_.songs()) ids.push_back(s.id);
    if (playlist_.sync(ids)) save_playlist();
    return changed;
}

void YourMusic::save_playlist() const {
    if (playlist_file_.empty()) return;
    const std::string text = playlist_.to_text();
    if (!gt3r::write_host_file(playlist_file_, ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()))) LOG_WARN("Could not save %s", playlist_file_.c_str());
}

void YourMusic::set_checked(u64 id, bool on) {
    if (playlist_.set_checked(id, on)) save_playlist();
}

SongSource YourMusic::playing_source() const { return library_.has_songs() && has_songs(playlist_.list()) ? source_ : SongSource::Game; }

RaceSong YourMusic::next(RacePlaylist* game, int last_piece) {
    const SongSource src = playing_source();
    RacePlaylist& mine = playlist_.list();
    if (src != SongSource::Game && game && playlist_.set_shuffle(game->shuffle())) save_playlist();
    bool yours = src == SongSource::Yours;
    if (src == SongSource::Both) {
        const bool game_listed = !game || has_songs(*game);
        yours = gt3r::race::pick_custom(game ? game->shuffle() : false, game ? songs_left(*game) : 1, songs_left(mine), game_listed, has_songs(mine), last_yours_,
                                        std::uniform_real_distribution<double>(0, 1)(rng_));
    }
    RaceSong s;
    if (yours) {
        const int row = mine.next();
        if (row < 0 || size_t(row) >= playlist_.ids().size()) return s;
        s.id = playlist_.ids()[size_t(row)];
        save_playlist();
    } else {
        s.piece = game ? game->next() : last_piece + 1;
        if (s.piece < 0) return RaceSong{};
    }
    last_yours_ = yours;
    return s;
}

bool YourMusic::can_play(const RaceSong& s, const RacePlaylist* game) const {
    const SongSource src = playing_source();
    if (s.yours()) return src != SongSource::Game && library_.find(s.id) && playlist_.checked(s.id);
    if (s.piece < 0 || src == SongSource::Yours) return false;
    return !game || (size_t(s.piece) < game->rows().size() && game->rows()[size_t(s.piece)].enabled);
}

float YourMusic::gain(u64 id) const {
    const gt3r::race::CustomSong* s = library_.find(id);
    const std::optional<float> disc = library_.disc_loudness();
    if (!s || !disc || !s->measured || s->peak <= 0) return 1.0f;
    const float g = std::clamp(std::pow(10.0f, (*disc - s->lufs) / 20.0f), 0.1f, 4.0f);
    // (never past full scale at the loudest level the game streams its racing pieces at)
    return std::min(g, 1.0f / (s->peak * std::max(loudest_level_, 0.01f)));
}

void YourMusic::started(const RaceSong& s) {
    if (playing_source() == SongSource::Game) return;
    // The next of the player's list ahead of time, this one (if it isn't yet) before it.
    RacePlaylist ahead = playlist_.list();
    if (const int row = ahead.next(); row >= 0 && size_t(row) < playlist_.ids().size()) library_.want_measured(playlist_.ids()[size_t(row)]);
    if (s.yours()) library_.want_measured(s.id);
}

} // namespace gt4r::audio
