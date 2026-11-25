#pragma once

// The game's textures by file, for texture packs (formats/texture_pack.h in the shared engine: a picture replaces a texture by the
// key of its pixels). A texture's key is of what the models are built from: a car's body set with each of its paints' patches on
// (formats/model/car_model.h), every other file's sets as the disc has them.

#include "gt4r/base/types.h"

#include <functional>
#include <optional>
#include <string>

namespace gt3r {
class Vfs;
namespace texture_pack {
class Dump;
struct FileTexture;
} // namespace texture_pack
} // namespace gt3r

namespace gt4r::settings {

using FileReader = std::function<std::optional<Bytes>(const std::string& path)>;

// Whether a file's textures can be replaced by a pack: the models' (cars, courses, figures, prizes ...). The menus' pictures, the
// fonts and the race display's are drawn from the files themselves.
bool has_pack_textures(const std::string& path);
// Calls `each` for every texture of a file with the name it is dumped under ("<path>" or "<path>/paint03").
void for_each_texture(const std::string& path, ByteSpan data, const FileReader& read, const std::function<void(const std::string& name, const gt3r::texture_pack::FileTexture&)>& each);
// The textures of a file into a dump; how many were new. False in `*ok`: a picture could not be written.
size_t dump_file(gt3r::texture_pack::Dump& dump, const std::string& path, ByteSpan data, const FileReader& read, bool* ok = nullptr);

} // namespace gt4r::settings
