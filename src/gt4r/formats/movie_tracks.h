#pragma once

// GT4's movies (`.pss`) through the engine's player, with one sound track.
//
// A movie's sound is PSS private stream 1 (0xBD) with a four-byte header `FF A1 00 <track>`
// before each packet's data; each track is a whole ADS stream of its own (its own `SShd`).
// The opening (`mpeg/main/opening_us.pss`) carries two, tracks 1 and 2, packet for packet;
// the ending and `pv` one, track 0. The engine's demuxer takes every private stream 1 packet
// as one stream, so two tracks come out interleaved: noise. Here the reads of the file are
// filtered: the packets of every track but the file's first are turned into padding (0xBE),
// which the demuxer passes over.

#include "gt4r/base/types.h"
#include "video/movie.h"

#include <memory>
#include <string>

namespace gt3r {
class Vfs;
}

namespace gt4r {

// The movie at `path` through the VFS, its sound the file's first track. Throws FormatError.
std::unique_ptr<gt3r::video::Movie> open_movie(gt3r::Vfs& vfs, const std::string& path);

// The read filter itself (`read` the file's bytes, `size` its size): what open_movie gives the
// engine's Movie. The track kept is the first one the reads come to.
gt3r::video::Movie::ReadFn first_sound_track_only(gt3r::video::Movie::ReadFn read, u64 size);

} // namespace gt4r
