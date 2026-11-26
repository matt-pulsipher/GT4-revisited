#include "gt4r/settings/texture_files.h"

#include "formats/texture_pack.h"
#include "gt4r/formats/model/car_model.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace gt4r::settings {

namespace {

bool ends_with(const std::string& s, const char* tail) {
    const size_t n = std::char_traits<char>::length(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

// The parts of a file with textures of their own: a car's body once in each of its paints, and what is around the body; any other
// file whole.
void parts(const std::string& path, ByteSpan data, const FileReader& read, const std::function<void(const std::string&, ByteSpan)>& each) {
    if (!car::CarFile::is_car(data)) return each(path, data);
    car::CarFile c;
    try {
        c = car::CarFile::parse(data);
    } catch (const std::exception&) {
        return each(path, data);
    }
    std::optional<Bytes> beside;
    ByteSpan pat;
    if (c.body_paint && c.body_paint < data.size()) pat = data.subspan(c.body_paint);
    else if ((beside = read(path + ".pat"))) pat = *beside;
    const u32 paints = car::paint_count(pat);
    if (!paints || c.body >= data.size()) return each(path, data);
    // The body ends where the file's next part begins.
    size_t end = data.size();
    for (u32 at : {c.info, c.collision, c.body_paint, c.wheel, c.wheel_paint, c.wing, c.tire[0], c.tire[1], c.driver})
        if (at > c.body) end = std::min(end, size_t(at));
    for (u32 p = 0; p < paints; ++p) {
        char name[24];
        std::snprintf(name, sizeof name, "/paint%02u", p);
        try {
            const Bytes painted = car::apply_paint(data.subspan(c.body, end - c.body), pat, p);
            each(path + name, painted);
        } catch (const std::exception&) {
            // (a patch past the part: the whole set after the body's start)
            try {
                const Bytes painted = car::apply_paint(data.subspan(c.body), pat, p);
                each(path + name, ByteSpan(painted).first(end - c.body));
            } catch (const std::exception&) {
            }
        }
    }
    if (c.body) each(path, data.first(c.body));
    if (end < data.size()) each(path, data.subspan(end));
}

} // namespace

bool has_pack_textures(const std::string& path) {
    for (const char* dir : {"car/", "crs/", "wheel/", "tire/", "wing/", "pit/", "piece/", "race/", "menu/", "character/", "projects/", "photo/"})
        if (path.rfind(dir, 0) == 0) return !ends_with(path, ".pat") && !ends_with(path, ".gpb") && !ends_with(path, ".adc") && !ends_with(path, ".pss") && !ends_with(path, ".mproject");
    return false;
}

void for_each_texture(const std::string& path, ByteSpan data, const FileReader& read, const std::function<void(const std::string&, const gt3r::texture_pack::FileTexture&)>& each) {
    parts(path, data, read, [&](const std::string& name, ByteSpan part) {
        gt3r::texture_pack::for_each_texture(name, part, [&](const gt3r::texture_pack::FileTexture& t) { each(name, t); });
    });
}

size_t dump_file(gt3r::texture_pack::Dump& dump, const std::string& path, ByteSpan data, const FileReader& read, bool* ok) {
    size_t n = 0;
    parts(path, data, read, [&](const std::string& name, ByteSpan part) { n += dump.add_file(name, part, ok); });
    return n;
}

} // namespace gt4r::settings
