#include "gt4r/app/mod_car_pictures.h"

#include "gt4r/app/app.h"
#include "gt4r/formats/db/mod_cars.h"

#include "base/file.h"
#include "base/log.h"
#include "formats/png_reader.h"

#include <algorithm>
#include <cmath>

namespace gt4r {

namespace {

// `from` resampled to w x h (each pixel the mean of the source's under it, or the nearest when it grows).
gt3r::Tex1::Image resized(const gt3r::PngImage& from, u32 w, u32 h) {
    gt3r::Tex1::Image out;
    out.width = w, out.height = h;
    out.rgba.resize(size_t(w) * h);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            const u32 x0 = x * from.width / w, x1 = std::max(x0 + 1, (x + 1) * from.width / w);
            const u32 y0 = y * from.height / h, y1 = std::max(y0 + 1, (y + 1) * from.height / h);
            u32 sum[4] = {0, 0, 0, 0}, n = 0;
            for (u32 sy = y0; sy < y1 && sy < from.height; ++sy)
                for (u32 sx = x0; sx < x1 && sx < from.width; ++sx, ++n) {
                    const u32 p = from.rgba[size_t(sy) * from.width + sx];
                    for (int c = 0; c < 4; ++c) sum[c] += (p >> (8 * c)) & 0xFF;
                }
            u32 v = 0;
            for (int c = 0; c < 4; ++c) v |= (n ? sum[c] / n : 0) << (8 * c);
            out.rgba[size_t(y) * w + x] = v;
        }
    return out;
}

} // namespace

std::optional<gt3r::Tex1::Image> mod_car_picture(App& app, const std::string& path,
                                                 const std::function<std::optional<gt3r::Tex1::Image>(const std::string&)>& disc) {
    db::SpecDb* sdb = path.find("piece/") == std::string::npos ? nullptr : app.db();
    if (!sdb) return std::nullopt;
    const std::optional<db::ModPicture> p = sdb->mod_picture(path);
    if (!p) return std::nullopt;
    std::optional<gt3r::Tex1::Image> stand_in = p->disc.empty() ? std::nullopt : disc(p->disc);
    if (!p->png.empty()) try {
            const std::optional<Bytes> file = gt3r::read_host_file(p->png);
            if (!file) throw FormatError("not read");
            const gt3r::PngImage png = gt3r::decode_png(*file, 2048);
            if (png.width && png.height)
                return stand_in ? resized(png, stand_in->width, stand_in->height) : resized(png, png.width, png.height);
        } catch (const std::exception& e) {
            LOG_WARN("Mod car picture %s: %s", p->png.c_str(), e.what());
        }
    return stand_in;
}

} // namespace gt4r
