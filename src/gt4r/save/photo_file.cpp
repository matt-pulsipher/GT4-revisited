#include "gt4r/save/photo_file.h"

#include "gt4r/save/card_icon.h"
#include "gt4r/save/codec.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>

namespace gt4r::save {

namespace fs = std::filesystem;

namespace {

// The letters of a folder's number, lowest first (the program's text at 0x694C80).
constexpr char kLetters[] = "Yz614Ef9UjIZquJNMtQhRxgTCH7yL0li5m+BsDcoewvp2P8VnKa3OXr-WSdbAFGk";

Bytes read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), {});
}
bool write_file(const fs::path& p, ByteSpan data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    return bool(out);
}
int kb(size_t bytes) { return int((bytes + 1023) / 1024); }

u32 chance() {
    static std::mt19937 random(u32(std::chrono::steady_clock::now().time_since_epoch().count()) ^ std::random_device{}());
    return random();
}

u64 get64(ByteSpan d, size_t o) { return u64(rd32(d, o)) | u64(rd32(d, o + 4)) << 32; }
void put32(Bytes& d, size_t o, u32 v) {
    for (int i = 0; i < 4; ++i) d[o + size_t(i)] = u8(v >> (8 * i));
}
std::string text_at(ByteSpan d, size_t o, size_t n) {
    std::string s;
    for (size_t i = 0; i < n && d[o + i]; ++i) s += char(d[o + i]);
    return s;
}
void put_text(Bytes& d, size_t o, size_t n, const std::string& s) {
    for (size_t i = 0; i + 1 < n && i < s.size(); ++i) d[o + i] = u8(s[i]);
}

} // namespace

Bytes PhotoInfo::encode() const {
    Bytes d(kInfoSize, 0);
    put32(d, 0x00, u32(car)), put32(d, 0x04, u32(car >> 32));
    put32(d, 0x08, u32(course)), put32(d, 0x0C, u32(course >> 32));
    put_text(d, 0x10, 0x40, name);
    put_text(d, 0x50, 0x40, course_file);
    put_text(d, 0x90, 0x48, car_name);
    d[0xD9] = 0xFF;
    put32(d, 0xDC, portrait ? 1 : 0);
    return d;
}

std::optional<PhotoInfo> PhotoInfo::decode(ByteSpan d) {
    if (d.size() < kInfoSize) return std::nullopt;
    PhotoInfo i;
    i.car = get64(d, 0), i.course = get64(d, 8);
    i.name = text_at(d, 0x10, 0x40), i.course_file = text_at(d, 0x50, 0x40), i.car_name = text_at(d, 0x90, 0x48);
    i.portrait = rd32(d, 0xDC) != 0;
    return i;
}

std::string folder_letters(u64 id) {
    std::string s;
    for (int i = 0; i < 7; ++i, id >>= 6) s += kLetters[id & 63];
    return s;
}

std::optional<u64> folder_id(const std::string& letters) {
    if (letters.size() != 7) return std::nullopt;
    u64 id = 0;
    for (size_t i = 0; i < 7; ++i) {
        const char* p = std::strchr(kLetters, letters[i]);
        if (!letters[i] || !p) return std::nullopt;
        id |= u64(p - kLetters) << (6 * i);
    }
    return id;
}

Bytes full_width_title(const std::string& s) {
    Bytes out;
    for (unsigned char c : s) {
        u16 code = 0x8140;
        if (c >= '0' && c <= '9') code = u16(0x824F + (c - '0'));
        else if (c >= 'A' && c <= 'Z') code = u16(0x8260 + (c - 'A'));
        else if (c >= 'a' && c <= 'z') code = u16(0x8281 + (c - 'a'));
        else if (c == '[') code = 0x816D;
        else if (c == ']') code = 0x816E;
        else if (c == '-') code = 0x817C;
        else if (c == '.') code = 0x8144;
        else if (c == ':') code = 0x8146;
        else if (c == '\'') code = 0x8166;
        out.push_back(u8(code >> 8)), out.push_back(u8(code));
    }
    return out;
}

// ----------------------------------------------------------------------------------------- the icon

namespace {

// Where an icon's texture starts; 0 when it is no such icon.
size_t texture_at(ByteSpan d) {
    size_t at = 0;
    auto need = [&](size_t n) { return at <= d.size() && d.size() - at >= n; };
    if (!need(20) || rd32(d, 0) != 0x10000) return 0;
    const u32 shapes = rd32(d, 4), vertices = rd32(d, 16);
    if (!shapes || shapes > 64 || vertices > 0x10000) return 0;
    at = 20 + size_t(vertices) * (8 * shapes + 16) + 16;
    if (!need(4)) return 0;
    const u32 frames = rd32(d, at);
    at += 4;
    for (u32 i = 0; i < frames; ++i) {
        if (!need(8)) return 0;
        const u32 keys = rd32(d, at + 4);
        at += 8 + size_t(keys) * 8;
    }
    return need(4) ? at : 0;
}

// How many pixels from `i` on are the same (0x1D40F0).
size_t run(const std::vector<u16>& p, size_t i) {
    size_t n = i + 1;
    while (n < p.size() && p[n] == p[i]) ++n;
    return n - i;
}

// The texture packed (0x1D4148): runs of two or more (four or more, or three before another run,
// inside a stretch of single pixels), else stretches as they are.
Bytes pack(const std::vector<u16>& p) {
    Bytes out(4, 0);
    auto word = [&out](u32 v) { out.push_back(u8(v)), out.push_back(u8(v >> 8)); };
    size_t i = 0, single = 0;
    bool stretch = false;
    auto give = [&](size_t end) {
        word(u32(0x10000 - single) & 0xFFFF);
        for (size_t k = end - single; k < end; ++k) word(p[k]);
    };
    while (i < p.size()) {
        const size_t n = run(p, i);
        if (!stretch) {
            if (n < 2) {
                ++i, single = n, stretch = true;
                continue;
            }
            word(u32(n)), word(p[i]);
            i += n;
            continue;
        }
        bool end = n >= 4;
        if (!end && n == 3) end = i + 3 >= p.size() ? false : run(p, i + 3) >= 2;
        if (!end) {
            ++i, ++single;
            continue;
        }
        give(i);
        word(u32(n)), word(p[i]);
        i += n, stretch = false, single = 0;
    }
    if (single) give(i);
    put32(out, 0, u32(out.size() - 4));
    return out;
}

} // namespace

Bytes icon_with_picture(ByteSpan icon, const std::vector<u16>& small) {
    constexpr size_t kSide = 128;
    const size_t at = texture_at(icon);
    if (!at || small.size() < size_t(kIconPicture) * kIconPicture) return {};
    const u32 flags = rd32(icon, 8);
    std::vector<u16> px;
    if (flags & 8) {
        const size_t end = std::min(icon.size(), at + 4 + size_t(rd32(icon, at)));
        for (size_t o = at + 4; o + 2 <= end && px.size() < kSide * kSide;) {
            const u16 n = rd16(icon, o);
            o += 2;
            if (n & 0x8000) {
                for (u32 k = 0x10000u - n; k && o + 2 <= end; --k, o += 2) px.push_back(rd16(icon, o));
            } else if (o + 2 <= end) {
                px.insert(px.end(), n, rd16(icon, o));
                o += 2;
            }
        }
    } else
        for (size_t o = at; o + 2 <= icon.size() && px.size() < kSide * kSide; o += 2) px.push_back(rd16(icon, o));
    if (px.size() < kSide * kSide) return {};
    px.resize(kSide * kSide);
    for (u16& v : px) v |= 0x8000;
    for (int y = 0; y < kIconPicture; ++y)
        std::copy_n(small.begin() + y * kIconPicture, kIconPicture, px.begin() + ptrdiff_t(y) * ptrdiff_t(kSide));
    Bytes out(icon.begin(), icon.begin() + ptrdiff_t(at));
    put32(out, 8, flags & ~8u);
    const Bytes packed = pack(px);
    if (packed.size() <= 0x7FFF) {
        put32(out, 8, rd32(out, 8) | 8);
        out.insert(out.end(), packed.begin(), packed.end());
    } else
        for (u16 v : px) out.push_back(u8(v)), out.push_back(u8(v >> 8));
    return out;
}

// ----------------------------------------------------------------------------------------- the card

PhotoCard::PhotoCard(std::string card, Reader disc, std::string product) : card_(std::move(card)), product_(std::move(product)), disc_(std::move(disc)) {}

std::vector<PhotoEntry> PhotoCard::list(char kind) const {
    std::vector<PhotoEntry> out;
    if (card_.empty()) return out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(card_, ec)) {
        const std::string name = e.path().filename().string();
        if (!e.is_directory(ec) || name.size() != product_.size() + 8 || name.rfind(product_, 0) != 0 || name[product_.size()] != kind) continue;
        const std::optional<u64> id = folder_id(name.substr(product_.size() + 1));
        if (!id) continue;
        PhotoEntry p;
        p.folder = name, p.kind = kind, p.number = int(*id >> 32 & 0x1FFFF), p.portrait = *id & 1;
        int files = 0, total = 0;
        std::error_code e2;
        for (const auto& f : fs::directory_iterator(e.path(), e2))
            if (f.is_regular_file(e2)) total += kb(size_t(f.file_size(e2))), ++files;
        p.kb = total + (files + 1) / 2 + 1;
        if (const Bytes rec = read_file(e.path() / "thumbnail"); rec.size() == kInfoFile)
            if (const std::optional<Bytes> plain = unscramble(rec, 0, FileKind::Info)) p.info = PhotoInfo::decode(*plain);
        const auto when = fs::last_write_time(e.path() / name, e2);
        if (!e2) {
            const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(when - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            const std::time_t t = std::chrono::system_clock::to_time_t(sys);
            if (const std::tm* tm = std::localtime(&t)) p.date = {tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec};
        }
        out.push_back(std::move(p));
    }
    std::sort(out.begin(), out.end(), [](const PhotoEntry& a, const PhotoEntry& b) { return a.number != b.number ? a.number < b.number : a.folder < b.folder; });
    return out;
}

std::optional<Bytes> PhotoCard::picture(const std::string& folder) const {
    if (card_.empty() || folder.find('/') != std::string::npos) return std::nullopt;
    const Bytes file = read_file(fs::path(card_) / folder / folder);
    const std::optional<Bytes> plain = unscramble(file, 0, FileKind::Photo);
    if (!plain || plain->size() < 4 || (*plain)[0] != 0xFF || (*plain)[1] != 0xD8) return std::nullopt;
    return plain;
}

std::optional<std::vector<u32>> PhotoCard::icon(const std::string& folder) const {
    if (card_.empty() || folder.find('/') != std::string::npos) return std::nullopt;
    return icon_picture(read_file(fs::path(card_) / folder / "icon0.ico"));
}

Bytes PhotoCard::make_icon(const std::vector<u16>& small, bool portrait) const {
    const std::optional<Bytes> t = disc_ ? disc_(portrait ? "icon/GT4_photo_3_to_4_.ico" : "icon/GT4_photo_4_to_3_.ico") : std::nullopt;
    if (!t) return {};
    Bytes out = icon_with_picture(*t, small);
    return out.empty() ? *t : out;
}

int PhotoCard::needed_kb(ByteSpan jpeg, const std::vector<u16>& small, bool portrait) const {
    // Two files (the picture, the record), icon.sys and the icon; an entry's two a KB, one more.
    const int files = kb(scrambled_size(jpeg.size())) + kb(kInfoFile) + 1 + kb(make_icon(small, portrait).size());
    const int entries = 2 + 1 + 1;
    return files + (entries + 1) / 2 + 1;
}

Result PhotoCard::save(const PhotoInfo& info, ByteSpan jpeg, const std::vector<u16>& small, const std::string& title) {
    if (card_.empty()) return Result::NoCard;
    if (jpeg.empty()) return Result::InvalidFile;
    // The lowest number no photo has (0x1CD5F8).
    std::vector<int> used;
    for (const PhotoEntry& e : list('P')) used.push_back(e.number);
    int number = 0;
    while (std::find(used.begin(), used.end(), number) != used.end()) ++number;
    if (number >= 999) return Result::DeviceError;
    const u64 id = u64(number) << 32 | (u64(chance()) & ~u64(1)) | (info.portrait ? 1 : 0);
    const std::string folder = product_ + "P" + folder_letters(id);
    const fs::path dir = fs::path(card_) / folder;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return Result::DeviceError;
    PhotoInfo rec = info;
    char name[16];
    std::snprintf(name, sizeof name, "P%04d", number);
    rec.name = name;
    if (!write_file(dir / "thumbnail", scramble(rec.encode(), 0, chance(), FileKind::Info))) return Result::DeviceError;
    if (!write_file(dir / folder, scramble(jpeg, 0, chance(), FileKind::Photo))) return Result::DeviceError;
    if (const Bytes icon = make_icon(small, info.portrait); !icon.empty()) write_file(dir / "icon0.ico", icon);
    if (std::optional<Bytes> sys = disc_ ? disc_("icon/icon.sys") : std::nullopt; sys && sys->size() >= 0x1C4) {
        // The title: "GRAN TURISMO 4" and the second line "Photo data[1]".
        Bytes& d = *sys;
        Bytes text = full_width_title("GRAN TURISMO 4");
        const Bytes second = full_width_title(title + "[" + std::to_string(number + 1) + "]");
        d[6] = u8(text.size()), d[7] = 0;
        text.insert(text.end(), second.begin(), second.end());
        if (text.size() > 0x43) text.resize(0x42);
        std::fill(d.begin() + 0xC0, d.begin() + 0x104, u8(0));
        std::copy(text.begin(), text.end(), d.begin() + 0xC0);
        for (size_t at : {size_t(0x104), size_t(0x144), size_t(0x184)}) {
            std::fill(d.begin() + ptrdiff_t(at), d.begin() + ptrdiff_t(at + 0x40), u8(0));
            std::memcpy(d.data() + at, "icon0.ico", 9);
        }
        d.resize(964);
        write_file(dir / "icon.sys", d);
    }
    return Result::Ok;
}

Result PhotoCard::remove(const std::string& folder) {
    if (card_.empty()) return Result::NoCard;
    if (folder.empty() || folder.find('/') != std::string::npos || folder.rfind(product_, 0) != 0) return Result::FileNotFound;
    std::error_code ec;
    fs::remove_all(fs::path(card_) / folder, ec);
    return ec ? Result::DeviceError : Result::Ok;
}

} // namespace gt4r::save
