// gt4tool photo: the photo locations and the photos on a memory card (docs/formats/photo.md).
//
//   gt4tool photo <disc> locations
//       every course with a photo location: its cells (spots), shapes, where the camera and the car start.
//   gt4tool photo <disc> location <course>
//       one location's cells and shapes.
//   gt4tool photo <disc> card <card folder> [outdir]
//       the photos of a card's folder: number, portrait, KB, car, course; with [outdir] their JPEG files.
//   gt4tool photo <disc> check <card folder>
//       each photo's record, icon and icon.sys made again from what they hold and compared, and the JPEG
//       decoded and coded again: the sizes of both.
//   gt4tool photo <disc> film <film file>
//       a film's data (a card's F folder's file) unscrambled, its chunks listed.
//   gt4tool photo <disc> import <card image> <card folder>
//       the photo and film folders of a .ps2 / .mcd card image (BASCUS-97328P..., ...F...) copied into a card folder.

#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/menu/jpeg.h"
#include "gt4r/photo/film.h"
#include "gt4r/photo/photo_course.h"
#include "gt4r/photo/picture.h"
#include "gt4r/save/card_icon.h"
#include "gt4r/save/codec.h"
#include "gt4r/save/photo_file.h"
#include "formats/ps2/memcard.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace gt4r::tool {

namespace {

Bytes read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), {});
}

std::optional<Bytes> disc_file(Disc& disc, const std::string& path) {
    const gt3r::Volume::Entry* e = disc.volume().find(path);
    if (!e) return std::nullopt;
    return disc.volume().read(*e);
}

int locations(Disc& disc, const std::string& only) {
    for (const std::string& name : disc.volume().list_dir("crs")) {
        if (!only.empty() && name != only) continue;
        const std::optional<Bytes> file = disc_file(disc, "crs/" + name);
        if (!file || !course::CourseFile::looks_like_course(*file)) continue;
        const ByteSpan s = course::CourseFile::parse(*file).section(*file, 51);
        if (s.empty()) continue;
        const photo::PhotoCourse c = photo::PhotoCourse::parse(s);
        int spots = 0;
        for (const photo::Cell& k : c.cells) spots += k.spot;
        std::printf("%-16s %2zu cells (%d spots) %2zu shapes  camera %.2f %.2f in cell ", name.c_str(), c.cells.size(), spots, c.shapes.size(), c.camera_start.x, c.camera_start.z);
        photo::Vec2 p = c.camera_start;
        const int at = c.cell_at(p, 0);
        std::printf("%d  car %.2f %.2f heading %.2f in shape %d  map at 0x%X\n", at, c.car_start.x, c.car_start.z, c.car_heading, c.car_shape_at(c.car_start, at), c.map_offset);
        if (only.empty()) continue;
        for (size_t i = 0; i < c.cells.size(); ++i) {
            const photo::Cell& k = c.cells[i];
            std::printf("  cell %zu%s shape %d, look at %.2f %.2f, eye %.2f .. %.2f m, %zu targets, car in", i, k.spot ? " (spot)" : "", k.shape, k.look.x, k.look.z, k.eye_low, k.eye_high, k.targets.size());
            for (u16 s2 : k.car_shapes) std::printf(" %u", s2);
            std::printf(", +0x20 %.2f\n", k.angle);
        }
        for (size_t i = 0; i < c.shapes.size(); ++i) {
            std::printf("  shape %zu:", i);
            for (const auto& o : c.shapes[i].outlines) std::printf(" %zu points", o.size());
            std::printf("\n");
        }
    }
    return 0;
}

int card(Disc& disc, const std::string& folder, const std::string& out) {
    save::PhotoCard c(folder, [&disc](const std::string& path) { return disc_file(disc, path); });
    for (char kind : {'P', 'F'})
        for (const save::PhotoEntry& e : c.list(kind)) {
            std::printf("%c %s number %d%s %d KB", kind, e.folder.c_str(), e.number, e.portrait ? " portrait" : "", e.kb);
            if (e.info) std::printf("  %s  car %s  course %s (%llx %llx)", e.info->name.c_str(), e.info->car_name.c_str(), e.info->course_file.c_str(), (unsigned long long)e.info->car, (unsigned long long)e.info->course);
            const std::optional<Bytes> jpeg = kind == 'P' ? c.picture(e.folder) : std::nullopt;
            if (jpeg) std::printf("  JPEG %zu bytes", jpeg->size());
            std::printf("\n");
            if (jpeg && !out.empty()) {
                std::ofstream f(out + "/" + e.folder + ".jpg", std::ios::binary);
                f.write(reinterpret_cast<const char*>(jpeg->data()), std::streamsize(jpeg->size()));
            }
        }
    return 0;
}

int check(Disc& disc, const std::string& folder) {
    auto reader = [&disc](const std::string& path) { return disc_file(disc, path); };
    save::PhotoCard c(folder, reader);
    for (const save::PhotoEntry& e : c.list('P')) {
        const std::filesystem::path dir = std::filesystem::path(folder) / e.folder;
        std::printf("%s:\n", e.folder.c_str());
        // The record.
        const Bytes rec = read_file(dir / "thumbnail");
        const std::optional<Bytes> plain = save::unscramble(rec, 0, save::FileKind::Info);
        std::printf("  record: %s", plain ? "reads" : "does not read");
        if (plain && e.info) {
            Bytes again = e.info->encode();
            int differ = 0;
            for (size_t i = 0; i < save::kInfoSize; ++i) differ += again[i] != (*plain)[i];
            std::printf(", %d bytes differ made again", differ);
        }
        std::printf("\n");
        // The scrambling: the same file from its own payload and first word.
        for (const auto& [name, kind] : {std::pair{std::string("thumbnail"), save::FileKind::Info}, std::pair{e.folder, save::FileKind::Photo}}) {
            const Bytes file = read_file(dir / name);
            const std::optional<Bytes> body = save::unscramble(file, 0, kind);
            bool same = false;
            if (body) {
                // (The head's random word is inside the file: every candidate is tried at its place.)
                for (size_t o = 0; o + 4 <= file.size() && !same; o += 1) {
                    const u32 r = rd32(file, o);
                    if (o > 0 && file.size() > 4096 && rd32(file, 0) != 0 && o % 1 != 0) continue;
                    const u64 n = file.size() - 4;
                    const double f = kind == save::FileKind::Info ? 0.468643 : 0.404;
                    if (std::abs(double(o) - double(n) * f) > 2) continue;
                    same = save::scramble(ByteSpan(body->data(), body->size()).subspan(0, body->size()), 0, r, kind) == file;
                }
            }
            std::printf("  %s scrambled again: %s\n", name.c_str(), same ? "the same bytes" : "differs");
        }
        // The icon.
        const Bytes icon = read_file(dir / "icon0.ico");
        const std::optional<Bytes> t = reader(e.portrait ? "icon/GT4_photo_3_to_4_.ico" : "icon/GT4_photo_4_to_3_.ico");
        if (const std::optional<std::vector<u32>> px = save::icon_picture(icon); px && t) {
            std::vector<u16> small;
            for (u32 v : *px) small.push_back(u16((v & 255) >> 3 | (v >> 8 & 255) >> 3 << 5 | (v >> 16 & 255) >> 3 << 10));
            const Bytes again = save::icon_with_picture(*t, small);
            size_t first = 0;
            while (first < again.size() && first < icon.size() && again[first] == icon[first]) ++first;
            std::printf("  icon made again: %zu bytes, the card's %zu%s\n", again.size(), icon.size(), again == icon ? ", the same" : (", first difference at " + std::to_string(first)).c_str());
        }
        // The picture.
        if (const std::optional<Bytes> jpeg = c.picture(e.folder)) {
            const menu::JpegPicture p = menu::decode_jpeg(*jpeg);
            std::vector<u8> rgb(size_t(p.width) * size_t(p.height) * 3);
            for (size_t i = 0; i < p.rgba.size(); ++i)
                for (int k = 0; k < 3; ++k) rgb[i * 3 + size_t(k)] = u8(p.rgba[i] >> (8 * k));
            const Bytes again = photo::encode_jpeg(rgb, p.width, p.height, 50, p.focus);
            size_t first = 0, size = jpeg->size();
            while (size && (*jpeg)[size - 1] == 0) --size;
            while (first < again.size() && first < size && again[first] == (*jpeg)[first]) ++first;
            const menu::JpegPicture q = menu::decode_jpeg(again);
            double sum = 0;
            for (size_t i = 0; i < p.rgba.size(); ++i) {
                const double d = double(p.rgba[i] & 255) - double(q.rgba[i] & 255);
                sum += d * d;
            }
            std::printf("  picture %d x %d, JPEG %zu bytes; coded again %zu, first difference at %zu, red off by %.2f (rms)\n", p.width, p.height, size, again.size(), first, std::sqrt(sum / double(p.rgba.size())));
            if (p.focus) std::printf("  focus %.4f %.4f in %.4f %.4f %.4f %.4f\n", p.focus->x, p.focus->y, p.focus->x0, p.focus->y0, p.focus->x1, p.focus->y1);
            const std::vector<u16> small = photo::icon_pixels(rgb, p.width, p.height);
            std::printf("  needs %d KB (on the card %d)\n", c.needed_kb(ByteSpan(jpeg->data(), size), small, e.portrait), e.kb);
        }
    }
    return 0;
}

// A chunk's tag where one reads (four capitals) and its size inside the span.
bool chunk_at(ByteSpan d, size_t at, size_t end, u32& size) {
    if (at + 8 > end) return false;
    for (size_t i = 0; i < 4; ++i)
        if (d[at + i] < 'A' || d[at + i] > 'Z') return false;
    size = rd32(d, at + 4);
    return size >= 8 && at + size <= end;
}
void dump_chunks(ByteSpan d, size_t at, size_t end, int depth) {
    while (at < end) {
        u32 size = 0;
        if (!chunk_at(d, at, end, size)) {
            std::printf("%*s+%04zX:", depth * 2, "", at);
            const size_t stop = [&] {
                for (size_t q = at + 1; q < end; ++q)
                    if (u32 z = 0; chunk_at(d, q, end, z)) return q;
                return end;
            }();
            for (size_t q = at; q < stop; ++q) std::printf(" %02X", d[q]);
            std::printf("\n");
            at = stop;
            continue;
        }
        std::printf("%*s%c%c%c%c %u\n", depth * 2, "", d[at], d[at + 1], d[at + 2], d[at + 3], size);
        dump_chunks(d, at + 8, at + size, depth + 1);
        at += size;
    }
}
int film(const std::string& path) {
    const std::optional<Bytes> d = save::unscramble(read_file(path), 0, save::FileKind::Film);
    if (!d) return std::fprintf(stderr, "%s: does not unscramble\n", path.c_str()), 1;
    std::printf("%zu bytes\n", d->size());
    dump_chunks(*d, 0, d->size(), 0);
    // Read as a film, and written again: the same bytes.
    const std::optional<photo::film::Film> f = photo::film::Film::decode(*d);
    if (!f) return std::puts("does not read as a film"), 1;
    const photo::film::Photo& p = f->photo;
    std::printf("photo: %ux%u x %ux%u, aspect %.4f, upright %d, %u pictures, shutter %.3f frames, filter %d, saturation %.2f, %+.1f EV, %.0f K, "
                "+0x12C %g, +0x13C %g\n", p.tile_w, p.tile_h, p.tiles_x, p.tiles_y, double(p.aspect), p.upright, p.pictures,
                double(p.shutter_frames), p.filter, double(p.saturation), double(p.exposure), double(p.kelvin), double(p.p12c), double(p.p13c));
    std::printf("race: %s (%s), %zu cars, %zu frames\n", f->course.c_str(), f->kind.c_str(), f->cars.size(), f->frames.size());
    for (const photo::film::Car& c : f->cars) std::printf("  car %s paint %s; %s %s %s %s\n", c.label.c_str(), c.paint.c_str(), c.looks[0].c_str(), c.looks[1].c_str(), c.looks[2].c_str(), c.looks[3].c_str());
    for (const photo::film::Frame& fr : f->frames) {
        const photo::film::Camera& c = fr.camera;
        std::printf("  camera eye (%.3f %.3f %.3f) target (%.3f %.3f %.3f) up (%.4f %.4f %.4f) fov %.3f aspect %.3f near %.3f far %.1f F%.1f 1/%.0f %+.1f EV %.0f K\n",
                    double(c.eye.x), double(c.eye.y), double(c.eye.z), double(c.target.x), double(c.target.y), double(c.target.z), double(c.up.x),
                    double(c.up.y), double(c.up.z), double(c.fov), double(c.aspect), double(c.near), double(c.far), double(c.f_number),
                    double(1 / c.shutter), double(c.exposure), double(c.kelvin));
        for (const photo::film::CarFrame& car : fr.cars) {
            const photo::film::Params& q = car.params;
            std::printf("    car at (%.3f %.3f %.3f) turn (%.3f %.3f %.3f) v (%.3f %.3f %.3f) rpm %.0f v18 %d v19 %g\n", double(q.at.x), double(q.at.y),
                        double(q.at.z), double(q.turn.x), double(q.turn.y), double(q.turn.z), double(q.velocity.x), double(q.velocity.y),
                        double(q.velocity.z), double(q.rpm), q.v18, double(q.v19));
            for (const auto& w : q.wheels)
                std::printf("      wheel turn (%.2f %.2f %.2f) at (%.3f %.3f %.3f) %g %g %g %g spun %g %g %g\n", double(w.turn.x), double(w.turn.y),
                            double(w.turn.z), double(w.at.x), double(w.at.y), double(w.at.z), double(w.v11), double(w.v12), double(w.v13),
                            double(w.v14), double(w.spun), double(w.v24), double(w.v25));
        }
    }
    const Bytes again = f->encode();
    const bool same = again.size() <= d->size() && std::equal(again.begin(), again.end(), d->begin()) &&
                      std::all_of(d->begin() + ptrdiff_t(again.size()), d->end(), [](u8 b) { return b == 0; });
    std::printf("written again: %zu bytes, %s\n", again.size(), same ? "the same" : "DIFFERENT");
    return same ? 0 : 1;
}

int import_photos(const std::string& image, const std::string& folder) {
    const Bytes data = read_file(image);
    if (!gt3r::ps2::is_card_image(data)) return std::fprintf(stderr, "%s: not a card image\n", image.c_str()), 1;
    int n = 0;
    for (const gt3r::ps2::McDir& d : gt3r::ps2::read_card(data)) {
        if (d.name.size() != 20 || d.name.compare(0, 12, "BASCUS-97328") != 0 || (d.name[12] != 'P' && d.name[12] != 'F')) continue;
        const std::filesystem::path dir = std::filesystem::path(folder) / d.name;
        std::filesystem::create_directories(dir);
        for (const gt3r::ps2::McFile& f : d.files) {
            std::ofstream out(dir / f.name, std::ios::binary);
            out.write(reinterpret_cast<const char*>(f.data.data()), std::streamsize(f.data.size()));
        }
        std::printf("%s: %zu files\n", d.name.c_str(), d.files.size()), ++n;
    }
    std::printf("%d folders\n", n);
    return 0;
}

} // namespace

int cmd_photo(Disc& disc, const std::vector<std::string>& args) {
    if (args.empty()) return std::puts("photo: locations | location <course> | card <folder> [outdir] | check <folder>"), 1;
    if (args[0] == "locations") return locations(disc, {});
    if (args[0] == "location" && args.size() > 1) return locations(disc, args[1]);
    if (args[0] == "card" && args.size() > 1) return card(disc, args[1], args.size() > 2 ? args[2] : std::string());
    if (args[0] == "check" && args.size() > 1) return check(disc, args[1]);
    if (args[0] == "film" && args.size() > 1) return film(args[1]);
    if (args[0] == "import" && args.size() > 2) return import_photos(args[1], args[2]);
    return 1;
}

} // namespace gt4r::tool
