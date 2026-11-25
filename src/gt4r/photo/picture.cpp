#include "gt4r/photo/picture.h"

#include "gt4r/save/codec.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::photo {

namespace {

constexpr float kPi = 3.14159274f;

} // namespace

Aspect aspect_by_name(const std::string& name) {
    if (name == "16x9") return {16.0f / 9.0f, 640, 480};
    if (name == "3x2") return {1.5f, 640, 480};
    if (name == "1x1") return {1.0f, 512, 512};
    return {4.0f / 3.0f, 640, 480};
}

std::vector<Sample> samples(int count) {
    save::Mt19937 mt(0x12345678);
    auto unit = [&mt] { return float(mt.next()) * 2.32830644e-10f; };
    std::vector<Sample> out;
    for (int i = 0; i < count; ++i) {
        Sample s;
        do {
            s.lens_x = unit() * 2 - 1;
            s.lens_y = unit() * 2 - 1;
        } while (1 < s.lens_x * s.lens_x + s.lens_y * s.lens_y);
        s.x = unit(), s.y = unit();
        out.push_back(s);
    }
    return out;
}

LensShift lens_shift(const Sample& s, float focus, float f_number, float film_mm, float aspect, float px, float py, int w, int h) {
    LensShift r;
    if (focus < 0.1f) focus = 10000;
    const float half = film_mm * 0.001f / std::sqrt(aspect * aspect + 1) * 0.5f; // metres
    const float radius = half * py / (f_number + f_number);                      // the focal length over twice the f-number
    r.move_x = s.lens_x * radius, r.move_y = s.lens_y * radius;
    const float shape = (float(w) / float(h)) / aspect; // a tile's shape against the picture's
    if (shape > 1) r.move_x /= shape;
    else r.move_y *= shape;
    // Half a pixel either way: in the tile's pixels.
    r.shear_x = r.move_x / focus * px + 2 * (s.x - 0.5f) / float(w);
    r.shear_y = r.move_y / focus * py + 2 * -(s.y - 0.5f) / float(h);
    return r;
}

float auto_focus(float distance, float f_number, float py) {
    constexpr float kFilm = 35, kAspect = 1.33333337f, kBlur = 0.00104166672f;
    const float height = kFilm * 0.001f / std::sqrt(kAspect * kAspect + 1); // metres
    const float blur = kBlur * (height + height), focal = height * 0.5f * py;
    const float d = distance, f2 = focal * focal;
    const float far = d * blur * f_number - f2;
    if (far == 0) return d;
    const float s = d * (blur * f_number * focal - f2) / far;
    return s < d ? d : s;
}

ColorFilter filter_by_name(const std::string& name) {
    return name == "mature" ? ColorFilter::Mature : name == "monochrome" ? ColorFilter::Monochrome : ColorFilter::None;
}
const char* filter_name(ColorFilter f) { return f == ColorFilter::Mature ? "mature" : f == ColorFilter::Monochrome ? "monochrome" : "none"; }

Correction correction(const Filters& f) {
    Correction c;
    const float gain = std::pow(2.0f, float(f.brightness) * 0.125f);
    const float shift = float(f.white_balance) * 2000.0f / 5.0f * 0.2f / 2000.0f;
    c.gamma[0] = gain * (1 - shift), c.gamma[1] = gain, c.gamma[2] = gain * (1 + shift);
    c.white = 1 - float(f.contrast) * 0.1f;
    if (f.color == ColorFilter::Monochrome) c.saturation = 0;
    else if (f.color == ColorFilter::Mature) c.saturation = 0.59999f;
    return c;
}

bool Correction::plain() const { return gamma[0] == 1 && gamma[1] == 1 && gamma[2] == 1 && black == 0 && white == 1 && saturation == 1; }

std::array<u8, 3> Correction::apply(u8 r, u8 g, u8 b) const {
    float v[3];
    const u8 in[3] = {r, g, b};
    for (int i = 0; i < 3; ++i) {
        const float x = float(in[i]) / 255.0f;
        if (!(black < x) || white == black) v[i] = 0;
        else v[i] = float(int(std::clamp(std::pow(std::min((x - black) / (white - black), 1.0f), 1.0f / std::max(gamma[i], 1e-3f)) * 255.0f, 0.0f, 255.0f)));
    }
    if (saturation == 1) return {u8(v[0]), u8(v[1]), u8(v[2])};
    const float gray = 0.3f * v[0] + 0.59f * v[1] + 0.11f * v[2];
    std::array<u8, 3> out{};
    for (int i = 0; i < 3; ++i) out[size_t(i)] = u8(std::clamp(saturation * v[i] + (1 - saturation) * gray, 0.0f, 255.0f));
    return out;
}

float Vignette::alpha(float x, float y) const {
    const float px = x * 320, py = y * 224;
    return std::clamp((std::sqrt(px * px + py * py) - inner) / std::max(outer - inner, 1.0f), 0.0f, 1.0f) * strength;
}

Vignette vignette(const Filters& f) { return {std::min(f.lightness * 0.7f * 0.01f, 1.0f), 200, 420}; }

int exposure_weight(int k) { return k <= 0 ? 0 : int((1.0f - 1.0f / (float(k) + 1.0f)) * 128.0f); }

int jpeg_quality(const std::string& name) { return name == "STANDARD" ? 50 : name == "SUPER_FINE" ? 95 : 80; }

// --------------------------------------------------------------------------------------------- the JPEG file

namespace {

constexpr u8 kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                            41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                            30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
// The JPEG standard's own example tables (its annex K).
constexpr u8 kLuma[64] = {16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,  14, 13, 16, 24, 40,  57,
                          69, 56, 14, 17, 22,  29,  51,  87,  80, 62, 18, 22, 37,  56,  68,  109, 103, 77, 24, 35, 55,  64,
                          81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
constexpr u8 kChroma[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                            99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};
constexpr u8 kDcLumaBits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
constexpr u8 kDcChromaBits[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
constexpr u8 kDcValues[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr u8 kAcLumaBits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D};
constexpr u8 kAcLumaValues[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1,
    0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A,
    0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3,
    0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3,
    0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA};
constexpr u8 kAcChromaBits[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
constexpr u8 kAcChromaValues[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1,
    0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A,
    0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA,
    0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA};

struct Huffman {
    u16 code[256]{};
    u8 length[256]{};
    Huffman(const u8 (&bits)[16], const u8* values) {
        u16 c = 0;
        size_t k = 0;
        for (int l = 0; l < 16; ++l, c <<= 1)
            for (int i = 0; i < bits[l]; ++i, ++k) code[values[k]] = c++, length[values[k]] = u8(l + 1);
    }
};

struct Writer {
    Bytes out;
    u32 bits = 0;
    int count = 0;
    void byte(int v) { out.push_back(u8(v)); }
    void word(int v) { byte(v >> 8), byte(v & 255); }
    void put(u32 v, int n) {
        for (int i = n - 1; i >= 0; --i) {
            bits = bits << 1 | (v >> i & 1);
            if (++count == 8) {
                byte(int(bits & 255));
                if ((bits & 255) == 255) byte(0);
                bits = 0, count = 0;
            }
        }
    }
    void flush() {
        while (count) put(1, 1);
    }
    void code(const Huffman& h, int v) { put(h.code[v], h.length[v]); }
};

int category(int v) {
    int n = 0;
    for (v = std::abs(v); v; v >>= 1) ++n;
    return n;
}

struct Coder {
    float cosine[8][8];
    u8 table[2][64];
    Coder(int quality) {
        for (int u = 0; u < 8; ++u)
            for (int x = 0; x < 8; ++x) cosine[u][x] = std::cos(float(2 * x + 1) * float(u) * kPi / 16) * (u ? 0.5f : 0.35355339f);
        const int q = std::clamp(quality, 1, 100), scale = q < 50 ? 5000 / q : 200 - 2 * q;
        for (int i = 0; i < 64; ++i) {
            table[0][i] = u8(std::clamp((int(kLuma[i]) * scale + 50) / 100, 1, 255));
            table[1][i] = u8(std::clamp((int(kChroma[i]) * scale + 50) / 100, 1, 255));
        }
    }
    void block(const float in[64], int which, int& dc, const Huffman& hdc, const Huffman& hac, Writer& w) const {
        float t[64], f[64];
        for (int y = 0; y < 8; ++y)
            for (int u = 0; u < 8; ++u) {
                float s = 0;
                for (int x = 0; x < 8; ++x) s += in[y * 8 + x] * cosine[u][x];
                t[y * 8 + u] = s;
            }
        for (int u = 0; u < 8; ++u)
            for (int v = 0; v < 8; ++v) {
                float s = 0;
                for (int y = 0; y < 8; ++y) s += t[y * 8 + u] * cosine[v][y];
                f[v * 8 + u] = s;
            }
        int z[64];
        for (int i = 0; i < 64; ++i) z[i] = int(std::lround(f[kZigzag[i]] / float(table[which][kZigzag[i]])));
        const int diff = z[0] - dc;
        dc = z[0];
        auto value = [&w](int v, int n) { w.put(u32(v < 0 ? v + (1 << n) - 1 : v), n); };
        int n = category(diff);
        w.code(hdc, n);
        value(diff, n);
        int run = 0;
        for (int i = 1; i < 64; ++i) {
            if (!z[i]) {
                ++run;
                continue;
            }
            for (; run > 15; run -= 16) w.code(hac, 0xF0);
            n = category(z[i]);
            w.code(hac, run << 4 | n);
            value(z[i], n);
            run = 0;
        }
        if (run) w.code(hac, 0);
    }
};

} // namespace

Bytes encode_jpeg(const std::vector<u8>& rgb, int w, int h, int quality, const std::optional<menu::JpegPicture::Focus>& focus) {
    if (w <= 0 || h <= 0 || rgb.size() < size_t(w) * size_t(h) * 3) return {};
    const Coder coder(quality);
    Writer o;
    o.word(0xFFD8);
    o.word(0xFFE0), o.word(16);
    for (u8 b : {'J', 'F', 'I', 'F', '\0'}) o.byte(b);
    o.word(0x0102), o.byte(0), o.word(16), o.word(16), o.word(0);
    if (focus) {
        o.word(0xFFE8), o.word(43);
        for (const char* p = "G4PI PDI"; *p; ++p) o.byte(*p);
        o.byte(0);
        auto le = [&o](u32 v) { o.byte(int(v & 255)), o.byte(int(v >> 8 & 255)), o.byte(int(v >> 16 & 255)), o.byte(int(v >> 24)); };
        le(1), le(3);
        for (float v : {focus->x, focus->y, focus->x0, focus->y0, focus->x1, focus->y1}) {
            u32 bits;
            std::memcpy(&bits, &v, 4);
            le(bits);
        }
    }
    for (int t = 0; t < 2; ++t) {
        o.word(0xFFDB), o.word(67), o.byte(t);
        for (int i = 0; i < 64; ++i) o.byte(coder.table[t][kZigzag[i]]);
    }
    auto huffman = [&o](int id, const u8 (&bits)[16], const u8* values) {
        int n = 0;
        for (u8 b : bits) n += b;
        o.word(0xFFC4), o.word(19 + n), o.byte(id);
        for (u8 b : bits) o.byte(b);
        for (int i = 0; i < n; ++i) o.byte(values[i]);
    };
    huffman(0x00, kDcLumaBits, kDcValues), huffman(0x01, kDcChromaBits, kDcValues);
    huffman(0x10, kAcLumaBits, kAcLumaValues), huffman(0x11, kAcChromaBits, kAcChromaValues);
    o.word(0xFFC0), o.word(17), o.byte(8), o.word(h), o.word(w), o.byte(3);
    for (int i = 0; i < 3; ++i) o.byte(i), o.byte(i ? 0x11 : 0x22), o.byte(i ? 1 : 0);
    o.word(0xFFDA), o.word(12), o.byte(3);
    for (int i = 0; i < 3; ++i) o.byte(i), o.byte(i ? 0x11 : 0x00);
    o.byte(0), o.byte(63), o.byte(0);
    const Huffman dc0(kDcLumaBits, kDcValues), dc1(kDcChromaBits, kDcValues), ac0(kAcLumaBits, kAcLumaValues), ac1(kAcChromaBits, kAcChromaValues);
    int dc[3] = {};
    for (int my = 0; my < h; my += 16)
        for (int mx = 0; mx < w; mx += 16) {
            float luma[256], cb[64] = {}, cr[64] = {};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    const u8* p = &rgb[(size_t(std::min(my + y, h - 1)) * size_t(w) + size_t(std::min(mx + x, w - 1))) * 3];
                    const float r = p[0], g = p[1], b = p[2];
                    luma[y * 16 + x] = 0.299f * r + 0.587f * g + 0.114f * b - 128;
                    cb[(y / 2) * 8 + x / 2] += (-0.168736f * r - 0.331264f * g + 0.5f * b) * 0.25f;
                    cr[(y / 2) * 8 + x / 2] += (0.5f * r - 0.418688f * g - 0.081312f * b) * 0.25f;
                }
            for (int k = 0; k < 4; ++k) {
                float b[64];
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x) b[y * 8 + x] = luma[(y + (k / 2) * 8) * 16 + x + (k % 2) * 8];
                coder.block(b, 0, dc[0], dc0, ac0, o);
            }
            coder.block(cb, 1, dc[1], dc1, ac1, o);
            coder.block(cr, 1, dc[2], dc1, ac1, o);
        }
    o.flush();
    o.word(0xFFD9);
    return std::move(o.out);
}

std::vector<u16> icon_pixels(const std::vector<u8>& rgb, int w, int h) {
    std::vector<u16> out(64 * 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const int x0 = x * w / 64, x1 = std::max(x0 + 1, (x + 1) * w / 64), y0 = y * h / 64, y1 = std::max(y0 + 1, (y + 1) * h / 64);
            u32 sum[3] = {}, n = 0;
            for (int j = y0; j < y1; ++j)
                for (int i = x0; i < x1; ++i, ++n)
                    for (int k = 0; k < 3; ++k) sum[k] += rgb[(size_t(j) * size_t(w) + size_t(i)) * 3 + size_t(k)];
            const u32 r = sum[0] / n >> 3, g = sum[1] / n >> 3, b = sum[2] / n >> 3;
            out[size_t(y) * 64 + size_t(x)] = u16(r | g << 5 | b << 10);
        }
    return out;
}

} // namespace gt4r::photo
