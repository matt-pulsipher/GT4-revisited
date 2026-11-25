#pragma once

// Gran Turismo 4 Prologue's game data (docs/formats/prologue.md "The saved game"): its Status
// written as raw parts, each padded to 16 bytes: the header, the garage's count, the profile's
// name, the license units, the course units, the options, the controller, LAN and network
// settings. The file on the card is this payload as it is (no scrambling).

#include "gt4r/menu/game.h"

namespace gt4r::save {

constexpr size_t kProloguePayloadSize = 0x1E7A0;
constexpr u32 kProloguePayloadMagic = 0xFF60BAD0;
// The parts' places.
constexpr size_t kPrologueName = 0x20, kPrologueNameSize = 0x10;
constexpr size_t kPrologueLicense = 0x30, kPrologueLicenseUnit = 0x2E0, kPrologueLicenseUnits = 48;
constexpr size_t kPrologueCourse = 0x8A30, kPrologueCourseUnit = 0x550, kPrologueCourseUnits = 64;
constexpr size_t kPrologueOptions = 0x1DE20;

// A new game's payload: what Prologue's constructors make (Status 0x401E90), its header filled in.
Bytes new_prologue_payload();
// The state as Prologue's payload: the game's saved image (a new game's when it holds none of
// Prologue's), the profile's name (`username`), the license units (`prologue_license`, when the
// state has them), the header's length and CRC.
Bytes write_prologue(const menu::GameState& state);
// Whether a payload is Prologue's game data: the magic, the length, the CRC (0x4021A8).
bool valid_prologue(ByteSpan payload);
// The payload into the state (its image, the name, the license units). False (the state
// untouched) when it is not valid.
bool read_prologue(ByteSpan payload, menu::GameState& state);

} // namespace gt4r::save
