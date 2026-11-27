// Ours: the player's own race music (audio/your_music.h): the songs' names in the config, the player's list (GT4's play
// list rules over the folder's songs, kept in a file), the source chosen and the mix, and a race's sound playing them, the
// songs heard again after a song before, a song changed on the pause, the next race's first song. No disc: the songs are
// WAV files made here; the folder's scan is the shared engine's.

#include "test_framework.h"

#include "audio/audio_engine.h"
#include "base/file.h"
#include "gt4r/audio/menu_sound.h"
#include "gt4r/audio/race_sound.h"
#include "gt4r/audio/race_tables.h"
#include "gt4r/audio/render.h"
#include "gt4r/audio/sound_library.h"
#include "gt4r/audio/your_music.h"
#include "gt4r/formats/sound/sound_index.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <set>
#include <thread>

using namespace gt4r;

namespace {

namespace fs = std::filesystem;

void put32(Bytes& b, u32 v) {
    for (int i = 0; i < 4; ++i) b.push_back(u8(v >> (8 * i)));
}
void put16(Bytes& b, u16 v) { b.push_back(u8(v)), b.push_back(u8(v >> 8)); }
void put_tag(Bytes& b, const char* id, const std::string& text) {
    b.insert(b.end(), id, id + 4);
    const u32 n = u32(text.size() + 1);
    put32(b, n);
    b.insert(b.end(), text.begin(), text.end());
    b.push_back(0);
    if (n & 1) b.push_back(0);
}

// A WAV file of half a second of a tone, with its title and artist in a LIST INFO chunk.
void write_song(const fs::path& path, const std::string& title, const std::string& artist, float hz) {
    const u32 rate = 22050, frames = rate / 2;
    Bytes data;
    for (u32 i = 0; i < frames; ++i) {
        const s16 v = s16(8000 * std::sin(6.2831853 * hz * i / rate));
        put16(data, u16(v)), put16(data, u16(v));
    }
    Bytes info;
    info.insert(info.end(), {'I', 'N', 'F', 'O'});
    put_tag(info, "INAM", title);
    put_tag(info, "IART", artist);
    Bytes f;
    f.insert(f.end(), {'R', 'I', 'F', 'F'});
    put32(f, 0);
    f.insert(f.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(f, 16), put16(f, 1), put16(f, 2), put32(f, rate), put32(f, rate * 4), put16(f, 4), put16(f, 16);
    f.insert(f.end(), {'L', 'I', 'S', 'T'});
    put32(f, u32(info.size()));
    f.insert(f.end(), info.begin(), info.end());
    f.insert(f.end(), {'d', 'a', 't', 'a'});
    put32(f, u32(data.size()));
    f.insert(f.end(), data.begin(), data.end());
    const u32 riff = u32(f.size() - 8);
    std::memcpy(f.data() + 4, &riff, 4);
    fs::create_directories(path.parent_path());
    gt3r::write_host_file(path.string(), f);
}

// A scratch folder of the test's own, emptied first.
fs::path scratch(const char* name) {
    const fs::path p = fs::temp_directory_path() / ("gt4r_test_your_music_" + std::string(name));
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p);
    return p;
}

// Polls until the folder's scan has landed.
void wait_for_scan(audio::YourMusic& m) {
    for (int i = 0; i < 500; ++i) {
        m.poll();
        if (!m.library().scanning()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace

TEST(your_music_song_names) {
    audio::RaceSong d;
    d.piece = 12;
    CHECK(d.text() == "disc:12" && audio::RaceSong::parse(d.text()) == d && !d.yours());
    audio::RaceSong y;
    y.id = 0x1234abcdef56789ull;
    CHECK(y.text() == "yours:1234abcdef56789" && audio::RaceSong::parse(y.text()) == y && y.yours());
    CHECK(!audio::RaceSong::parse("").valid() && !audio::RaceSong::parse("yours:zz").valid() && !audio::RaceSong::parse("disc:-3").valid());
    CHECK(audio::parse_song_source("") == audio::SongSource::Yours && audio::parse_song_source("game") == audio::SongSource::Game &&
          audio::parse_song_source("both") == audio::SongSource::Both);
}

// The player's list: GT4's rules over the songs' rows; songs kept by id when the folder changes (their check marks), new
// ones checked; the file read back as it was written.
TEST(your_music_playlist) {
    audio::YourPlaylist p;
    CHECK(p.sync({10, 20, 30, 40}));
    CHECK(!p.sync({10, 20, 30, 40}));
    CHECK(p.checked_count() == 4);
    std::vector<u64> order;
    for (int i = 0; i < 4; ++i) order.push_back(p.ids()[size_t(p.list().next())]);
    CHECK((order == std::vector<u64>{10, 20, 30, 40})); // (in order: the rows' own)
    CHECK(p.set_checked(20, false) && !p.checked(20) && p.checked_count() == 3);
    order.clear();
    for (int i = 0; i < 6; ++i) order.push_back(p.ids()[size_t(p.list().next())]);
    CHECK((order == std::vector<u64>{10, 30, 40, 10, 30, 40}));
    // A song gone, one new: the others keep their marks, the new one is checked.
    CHECK(p.sync({10, 20, 40, 50}));
    CHECK(p.checked(10) && !p.checked(20) && p.checked(40) && p.checked(50) && !p.checked(30));
    // Shuffled: every checked song once a round, in an order of the program's generator.
    CHECK(p.set_shuffle(true) && !p.set_shuffle(true));
    std::set<u64> round;
    for (int i = 0; i < 3; ++i) round.insert(p.ids()[size_t(p.list().next())]);
    CHECK((round == std::set<u64>{10, 40, 50}));
    p.list().next(); // (the next round under way: the position kept in the file)
    const std::string text = p.to_text();
    const audio::YourPlaylist q = audio::YourPlaylist::parse(text);
    CHECK(q.to_text() == text);
    CHECK(q.list().position() == p.list().position() && q.list().shuffle());
    CHECK(audio::YourPlaylist::parse("something else").ids().empty());
}

// The folder's songs, the sources, and a race's sound playing them.
TEST(your_music_race) {
    const fs::path dir = scratch("race");
    write_song(dir / "music" / "Album" / "one.wav", "First Song", "Somebody", 440);
    write_song(dir / "music" / "two.wav", "Second Song", "", 660);
    gt3r::write_host_file((dir / "music" / "three.m4a").string(), Bytes(64, 0)); // (a format that doesn't play: counted)
    audio::YourMusic music;
    music.set_files(dir.string() + "/");
    music.set_folder((dir / "music").string());
    wait_for_scan(music);
    CHECK(music.library().songs().size() == 2 && music.library().unplayable() == 1);
    CHECK(music.playlist().ids().size() == 2 && music.playlist().checked_count() == 2);
    CHECK(fs::exists(dir / "music_playlist.txt"));
    const gt3r::race::CustomSong& first = music.library().songs()[0];
    CHECK(first.title == "First Song" && first.artist == "Somebody");

    // The sources: the player's alone, the game's alone, both in turn (the game's list in order).
    audio::RacePlaylist game = audio::RacePlaylist::in_order(8, {5, 2});
    music.set_source(audio::SongSource::Yours);
    CHECK(music.next(&game).yours() && music.next(&game).yours());
    music.set_source(audio::SongSource::Game);
    CHECK(music.next(&game).piece == 5);
    music.set_source(audio::SongSource::Both);
    const audio::RaceSong a = music.next(&game), b = music.next(&game), c = music.next(&game);
    CHECK(a.yours() != b.yours() && b.yours() != c.yours());
    // Nothing of the player's checked: the game's list plays.
    music.set_checked(music.playlist().ids()[0], false), music.set_checked(music.playlist().ids()[1], false);
    CHECK(music.playing_source() == audio::SongSource::Game && music.next(&game).piece >= 0);
    music.set_checked(music.playlist().ids()[0], true), music.set_checked(music.playlist().ids()[1], true);

    // A race's sound: the player's songs from their files at the racing level, the songs heard again after a song before,
    // a song changed on the pause waiting silent, the first song again in the replay.
    gt3r::Vfs vfs(nullptr);
    audio::OfflineSound s(false);
    std::vector<std::string> files;
    bool playing = false, paused = false;
    audio::StreamPlayer player;
    player.play = [&](const std::string& path, bool, float) { return files.push_back(path), playing = true; };
    player.play_file = [&](const std::string& path, float level, float trim) { return files.push_back(path), playing = level > 0 && trim > 0; };
    player.stop = [&] { playing = false; };
    player.pause = [&](bool p) { paused = p; };
    player.playing = [&] { return playing; };
    music.set_source(audio::SongSource::Yours);
    {
        audio::RaceSound sound(vfs, s.mixer(), s.driver(), nullptr, audio::RaceSoundTables{}, player);
        sound.set_playlist(std::make_shared<audio::RacePlaylist>(game));
        sound.set_your_music(&music);
        sound.rolling();
        CHECK(files.size() == 1 && sound.now_playing().yours());
        const audio::RaceSong opening = sound.now_playing();
        std::string title, artist;
        CHECK(sound.song_info(opening, title, artist) && !title.empty());
        CHECK(sound.skip_song(1) && files.size() == 2 && sound.now_playing() != opening);
        const audio::RaceSong second = sound.now_playing();
        CHECK(sound.skip_song(-1) && sound.now_playing() == opening);
        CHECK(sound.skip_song(1) && sound.now_playing() == second); // (heard again)
        sound.pause(true);
        CHECK(paused && sound.skip_song(1) && paused); // (on the pause: it waits silent)
        sound.pause(false);
        CHECK(!paused);
        // The song ends: the list's next.
        const size_t before = files.size();
        playing = false;
        sound.update(std::span<const audio::CarSoundState>{}, audio::SoundListener{}, -1);
        CHECK(files.size() == before + 1);
        CHECK(music.last_song() == sound.now_playing());
        sound.replay();
        CHECK(sound.now_playing() == opening);
        // The next race starts with the song the last ended on ([music] next_race = same).
        music.set_same_song(true);
        const audio::RaceSong last = music.last_song();
        sound.begin_run(audio::RaceMusic::Race, -1);
        CHECK(sound.now_playing() == last);
        music.set_same_song(false);
    }
    // The list read back by a new start (its file): the same places and check marks.
    {
        audio::YourMusic again;
        again.set_files(dir.string() + "/");
        again.set_folder((dir / "music").string());
        wait_for_scan(again);
        CHECK(again.playlist().to_text() == music.playlist().to_text());
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// A mod's recording of a piece of the menus' sequenced music plays in its place (by the piece's file name), looping at the
// menus' stream level; the group's next piece without one is the sequence again; a stop stops it.
TEST(your_music_menu_recordings) {
    Disc* disc = nullptr;
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && d->volume().find("music/us/mid.inf")) disc = d;
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    gt3r::Vfs vfs(&disc->volume());
    audio::OfflineSound s(false);
    auto library = std::make_shared<audio::SoundLibrary>(vfs);
    audio::MenuSound menu(library, s.driver());
    std::vector<std::string> asked;
    std::string playing;
    float level = 0;
    audio::MusicRecordings r;
    r.find = [&](const std::string& piece) {
        asked.push_back(piece);
        return piece == "gt4mn-01" ? std::string("/somewhere/gt4mn-01.flac") : std::string();
    };
    r.play = [&](const std::string& file, float l) { return playing = file, level = l, true; };
    r.stop = [&] { playing.clear(); };
    r.playing = [&] { return !playing.empty(); };
    menu.set_recordings(r);
    CHECK(menu.start_music("map"));
    CHECK(!asked.empty() && asked.back() == "gt4mn-01" && playing == "/somewhere/gt4mn-01.flac");
    const float stream = library->levels().level(1, sound::VolumeTable::Stream);
    CHECK(std::fabs(level - stream * 115.0f / 128.0f) < 1e-6f);
    CHECK(menu.music_group() == "map" && !menu.recording().empty() && !s.driver().music_active());
    CHECK(menu.keep_music("map") && playing == "/somewhere/gt4mn-01.flac"); // (plays on)
    menu.stop_music();
    CHECK(playing.empty() && menu.recording().empty());
    // The map's next piece has none: its sequence.
    CHECK(menu.start_music("map") && asked.back() == "gt4mn-02" && playing.empty() && s.driver().music_active());
    menu.stop_music();
}

// The disc's racing pieces measured through the shared engine's .ads reader (GT4's streams), and a song's gain from them:
// toward their loudness, never past full scale at the loudest stream level.
TEST(your_music_disc_loudness) {
    Disc* disc = nullptr;
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && d->volume().find("bgm/us/ads.inf")) disc = d;
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    gt3r::Vfs vfs(&disc->volume());
    audio::SoundLibrary library(vfs);
    const int racing = sound::stream_group("racing");
    std::vector<gt3r::race::MusicLibrary::StreamOpener> songs;
    for (const int n : {16}) { // (race_35: the first of the US list)
        CHECK(vfs.exists(library.stream_file(racing, n)));
        songs.push_back(audio::disc_piece_opener(vfs, library.stream_file(racing, n)));
    }
    const fs::path dir = scratch("loudness");
    write_song(dir / "music" / "quiet.wav", "Quiet", "", 440);
    audio::YourMusic music;
    music.set_folder((dir / "music").string());
    wait_for_scan(music);
    music.library().set_disc_songs("test", std::move(songs));
    const u64 id = music.library().songs().at(0).id;
    music.library().want_measured(id);
    for (int i = 0; i < 20000 && (!music.library().disc_loudness() || !music.song(id)->measured); ++i) {
        music.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::printf("  disc measured %d, the song measured %d, still measuring %d\n", int(music.library().disc_loudness().has_value()), int(music.song(id) && music.song(id)->measured), int(music.library().measuring()));
    CHECK(music.library().disc_loudness().has_value() && music.song(id) && music.song(id)->measured);
    if (music.library().disc_loudness() && music.song(id)) {
        const float disc_lufs = *music.library().disc_loudness();
        const gt3r::race::CustomSong& s = *music.song(id);
        std::printf("  the pieces %.1f LUFS, the song %.1f LUFS (peak %.3f): gain %.2f\n", double(disc_lufs), double(s.lufs), double(s.peak), double(music.gain(id)));
        CHECK(disc_lufs > -20 && disc_lufs < -4);
        const float want = std::min(std::clamp(std::pow(10.0f, (disc_lufs - s.lufs) / 20.0f), 0.1f, 4.0f), 1.0f / (s.peak * 0.3f));
        CHECK(std::fabs(music.gain(id) - want) < 1e-4f);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
