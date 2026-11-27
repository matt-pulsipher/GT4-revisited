#pragma once

// Ours: the player's own race music (Settings > Audio > Your music). MP3, FLAC, WAV and Ogg
// Vorbis files in a music folder (by default the user data folder's music/, at any depth) play
// as race songs beside the disc's racing pieces:
//   The game's  the game's own play list only (race_playlist.h, the music list of the game's jukebox)
//   Yours       the player's songs only, from a play list of the same kind (GT4's rules)
//   Both        both lists, mixed song by song: shuffled, each song drawn from the two in
//               proportion to the songs each still has in its round; in order, one of each in turn
// The order or shuffle is the game's own list's (its Play mode on the jukebox's music list,
// the option block's shuffle), for both lists. Without any songs of the player's the game's list
// plays whatever was chosen. [music] source = game | yours | both; unset: Yours, which is the
// game's own until a song is found.
//
// The folder's scan, the songs' tags and their loudness are the shared engine's
// (gt3r::race::MusicLibrary): each song plays as loud as the disc's racing pieces on average
// (measured on a thread, as they are asked for; see race/custom_music.h). The player's list is kept
// in <user data>/music_playlist.txt: each song's check mark, its place, whether it played this
// round, the list's position and seed.
//
// [music] next_race = same: a race (or a retry, or the replay of another run) starts with the
// song the last run ended on, from its start; unset or `next`, the next song of the list, as the
// game does.
//
//   audio::YourMusic music;
//   music.set_files(data_dir);  music.set_folder(data_dir + "music");
//   every frame:  music.poll();
//   a race:       race_sound.set_your_music(&music);

#include "gt4r/audio/race_playlist.h"
#include "gt4r/base/types.h"

#include "race/custom_music.h"

#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r {
class Vfs;
}

namespace gt4r::audio {

// One race song: a piece of the disc's racing group, or one of the player's songs.
struct RaceSong {
    int piece = -1; // the racing group's piece
    u64 id = 0;     // a song of the music folder (gt3r::race::CustomSong::id), when piece < 0
    bool yours() const { return piece < 0 && id != 0; }
    bool valid() const { return piece >= 0 || id != 0; }
    bool operator==(const RaceSong&) const = default;
    // "disc:<piece>" or "yours:<id in hex>"; empty for none.
    std::string text() const;
    static RaceSong parse(std::string_view text);
};

enum class SongSource : u8 { Game, Yours, Both };
SongSource parse_song_source(std::string_view s); // "game", "both"; anything else (unset too) Yours
std::string_view song_source_name(SongSource s);  // "game", "yours", "both"

// The player's list: GT4's play list (RacePlaylist) with a row per song of the music folder, in
// the folder's order.
class YourPlaylist {
public:
    const RacePlaylist& list() const { return list_; }
    RacePlaylist& list() { return list_; }
    const std::vector<u64>& ids() const { return ids_; }
    int row_of(u64 id) const;
    // The folder's songs now: rows kept by id (their check marks), new songs checked; when the
    // songs changed, the order is made again (from the first place). True when anything changed.
    bool sync(const std::vector<u64>& ids);
    // The game's list's choice: in order or shuffled (the order made again when it changes).
    bool set_shuffle(bool shuffle);
    // A song checked or not (the order made again).
    bool set_checked(u64 id, bool on);
    bool checked(u64 id) const;
    size_t checked_count() const;

    std::string to_text() const;
    static YourPlaylist parse(std::string_view text);

private:
    void rebuild();
    RacePlaylist list_;
    std::vector<u64> ids_;
};

// A disc piece (an .ads) for the library's measuring (MusicLibrary::set_disc_songs): read whole on the measuring thread, then
// decoded from memory (the disc's small reads one by one take many times as long).
gt3r::race::MusicLibrary::StreamOpener disc_piece_opener(gt3r::Vfs& vfs, std::string path);

// The songs left in a list's round (enabled and not played), and whether it has any enabled.
size_t songs_left(const RacePlaylist& list);
bool has_songs(const RacePlaylist& list);

class YourMusic {
public:
    YourMusic();
    ~YourMusic();
    YourMusic(const YourMusic&) = delete;
    YourMusic& operator=(const YourMusic&) = delete;

    gt3r::race::MusicLibrary& library() { return library_; }
    const gt3r::race::MusicLibrary& library() const { return library_; }

    // Where the player's list and the remembered songs are kept (the user data folder; "" for
    // nowhere: the tests').
    void set_files(const std::string& data_dir);
    // The folder to play from (a host path; "" for none); a new one is scanned at once.
    void set_folder(const std::string& folder);
    void rescan() { library_.rescan(); }
    // Every frame: a finished scan taken in, the list kept in step and saved. True when the songs changed.
    bool poll();

    SongSource source() const { return source_; }
    void set_source(SongSource s) { source_ = s; }
    // The source that plays: the chosen one, or the game's while there are no songs of the player's checked.
    SongSource playing_source() const;
    bool same_song() const { return same_song_; }
    void set_same_song(bool same) { same_song_ = same; }

    YourPlaylist& playlist() { return playlist_; }
    const YourPlaylist& playlist() const { return playlist_; }
    void save_playlist() const;
    // A song checked or not on the list (saved).
    void set_checked(u64 id, bool on);

    // The next race song by the source: from the game's list `game` (its next(); null: the
    // group's pieces in turn after `last_piece`), from the player's, or from the mix. The player's
    // list follows the game's list's order or shuffle. Not valid when neither has a song.
    RaceSong next(RacePlaylist* game, int last_piece = -1);
    // Whether `s` may play now (a song of the player's still in the folder and checked, a disc
    // piece while the source has the game's).
    bool can_play(const RaceSong& s, const RacePlaylist* game) const;

    const gt3r::race::CustomSong* song(u64 id) const { return library_.find(id); }
    // The gain one of the player's songs plays at (1 until measured).
    float gain(u64 id) const;
    // A song started: it and the next of the player's list measured soon.
    void started(const RaceSong& s);
    // The loudest level the game streams a racing piece at (the mix table's stream level at the
    // full option, 0.3 on the US disc): a song's gain keeps its loudest sample under full scale there.
    void set_loudest_level(float level) { loudest_level_ = level; }
    // Every frame a race runs (the disc's pieces are measured between races).
    void racing() { library_.racing(); }

    // The song the last run ended on ([music] next_race = same).
    const RaceSong& last_song() const { return last_; }
    void set_last_song(const RaceSong& s) { last_ = s; }

private:
    gt3r::race::MusicLibrary library_;
    YourPlaylist playlist_;
    std::string playlist_file_;
    bool playlist_read_ = false;
    SongSource source_ = SongSource::Yours;
    bool same_song_ = false;
    float loudest_level_ = 0.3f;
    bool last_yours_ = false; // Both in order: whose turn
    RaceSong last_;
    std::mt19937_64 rng_;
};

} // namespace gt4r::audio
