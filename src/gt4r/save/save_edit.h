#pragma once

// A saved game opened to be changed and written back (the save editor): its files read, the state they hold, and the files of a
// changed state. Only what the change is of changes: the game data's payload takes the bytes the two states write differently
// (its CRC and scrambling made again as the program makes them), the garage file its money and the slots of the cars whose
// records changed; the key, the serials and the stamps that tie the two files stay. Every write is checked by reading it back
// as the program does, a backup is kept of the files as they were, and nothing is written when another part wrote the save
// meanwhile. docs/SAVE_EDITOR.md, docs/formats/save.md.

#include "gt4r/save/game_data.h"

#include <optional>
#include <string>
#include <vector>

namespace gt4r::save {

// Where a save is: a folder of its files (a card's BASCUS-97328GAMEDATA), or a memory card image holding that folder.
struct EditPlace {
    enum class Kind { Folder, Image } kind = Kind::Folder;
    std::string path; // the folder of the files; the image's file
};
// A path the player picked as a place: a folder of the files, a folder holding the product's folder, the game data file
// itself, a card image. None when it is none of those (`why`: what it is not).
std::optional<EditPlace> edit_place(const std::string& path, const std::string& product, std::string* why = nullptr);

// The save's two changing files.
struct EditFiles {
    Bytes game;   // the game data file (Prologue's: its payload)
    Bytes garage; // the garage file (Prologue has none)
};

class SaveDocument {
public:
    // Opens a save. None, with why in plain words, when it is not there or not whole: the checks are the program's (the CRCs,
    // the magic and length, the garage's key and serial).
    static std::optional<SaveDocument> open(const EditPlace& place, const Codes& codes, const std::string& product, std::string* why = nullptr);
    // The same from files in memory (no place: write() refuses).
    static std::optional<SaveDocument> from_files(EditFiles files, const Codes& codes, const std::string& product, std::string* why = nullptr);

    menu::GameState state; // what is changed
    const menu::GameState& loaded() const { return loaded_; }
    const EditPlace& place() const { return place_; }
    bool prologue() const { return prologue_; }
    // Whether the state would write other bytes than the files hold.
    bool changed() const;
    // The files the state as it is now would be written as. None when they do not read back as the state (`why`).
    std::optional<EditFiles> files(std::string* why = nullptr) const;

    struct Written {
        bool ok = false;
        std::string message; // what went wrong, in plain words
        std::string backup;  // the backup kept ("" : none made)
    };
    // Writes the changed state. Before the first write of this document the files are copied into
    // `<backups>/<date and time> <name>` ("" : no backup is made). Refused when the files on disk are no longer the ones
    // opened. After it the document stands on the files as written.
    Written write(const std::string& backups);
    const std::string& backup() const { return backup_; }
    // The files read again and the changes dropped; false when they no longer open.
    bool reload(std::string* why = nullptr);

private:
    EditPlace place_;
    std::string product_;
    Codes codes_;
    EditFiles files_;
    Bytes payload_; // the game data, unscrambled, as it was read
    menu::GameState loaded_;
    bool prologue_ = false;
    std::string backup_;

    bool decode(std::string* why);
    Bytes changed_payload() const;
};

// The files of a place: its game data file and garage file. `why` when they cannot be read.
std::optional<EditFiles> read_files(const EditPlace& place, const std::string& product, std::string* why = nullptr);
// Writes them back where they came from, each file replaced whole; a card image keeps every byte but those files' data (and
// the spare bytes of the pages it changed).
bool write_files(const EditPlace& place, const std::string& product, const EditFiles& files, std::string* why = nullptr);

// A file of a card image's folder replaced by data of the same length, the rest of the image as it is (the error-correcting
// bytes of a changed page made again). None when the image has no such file or the length differs.
std::optional<Bytes> replace_in_card(ByteSpan image, const std::string& folder, const std::string& file, ByteSpan data, std::string* why = nullptr);

} // namespace gt4r::save
