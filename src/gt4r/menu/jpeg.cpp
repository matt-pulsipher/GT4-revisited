#include "gt4r/menu/jpeg.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace gt4r::menu {

namespace {

[[noreturn]] void fail(const char* what) { throw FormatError(std::string("JPEG: ") + what); }

struct Huffman {
    // Codes of up to 8 bits looked up at once; longer ones by the lengths' first codes.
    std::array<u16, 256> fast{}; // length << 8 | symbol; 0: longer
    std::array<s32, 18> max_code{};
    std::array<s32, 17> offset{};
    std::vector<u8> symbols;
    bool set = false;

    void build(ByteSpan d) {
        if (d.size() < 16) fail("Huffman table cut short");
        size_t n = 0;
        for (int i = 0; i < 16; ++i) n += d[size_t(i)];
        if (d.size() < 16 + n || n > 256) fail("Huffman table cut short");
        symbols.assign(d.begin() + 16, d.begin() + 16 + ptrdiff_t(n));
        fast.fill(0);
        s32 code = 0, k = 0;
        for (int len = 1; len <= 16; ++len) {
            offset[size_t(len)] = k - code;
            for (int i = 0; i < d[size_t(len - 1)]; ++i, ++code, ++k) {
                if (len <= 8)
                    for (int f = 0; f < 1 << (8 - len); ++f) {
                        const size_t at = size_t(code << (8 - len) | f);
                        if (at < 256) fast[at] = u16(len << 8 | symbols[size_t(k)]);
                    }
            }
            max_code[size_t(len)] = code; // one past the last of this length
            code <<= 1;
        }
        set = true;
    }
};

struct Bits {
    ByteSpan d;
    size_t at;
    u32 hold = 0;
    int count = 0;

    void fill(int want) {
        while (count < want) {
            u8 b = 0;
            // (Past a marker or the end: zeros.)
            if (at < d.size()) {
                b = d[at];
                if (b == 0xFF) {
                    const u8 next = at + 1 < d.size() ? d[at + 1] : u8(0xD9);
                    if (next == 0) at += 2;
                    else b = 0;
                } else ++at;
            }
            hold = hold << 8 | b, count += 8;
        }
    }
    u32 peek(int n) {
        fill(n);
        return hold >> (count - n) & ((1u << n) - 1);
    }
    s32 take(int n) {
        if (!n) return 0;
        const u32 v = peek(n);
        count -= n;
        return s32(v);
    }
    // A value of n bits as JPEG codes signed ones.
    s32 extend(int n) {
        const s32 v = take(n);
        return n && v < 1 << (n - 1) ? v - (1 << n) + 1 : v;
    }
    u8 symbol(const Huffman& h) {
        const u16 f = h.fast[peek(8)];
        if (f) {
            count -= f >> 8;
            return u8(f);
        }
        s32 code = take(8);
        for (int len = 9; len <= 16; ++len) {
            code = code << 1 | take(1);
            if (code < h.max_code[size_t(len)]) {
                const s32 k = code + h.offset[size_t(len)];
                if (k >= 0 && size_t(k) < h.symbols.size()) return h.symbols[size_t(k)];
            }
        }
        fail("bad code");
    }
    void restart() {
        count = 0, hold = 0;
        while (at + 1 < d.size() && !(d[at] == 0xFF && d[at + 1] >= 0xD0 && d[at + 1] <= 0xD7)) ++at;
        at += 2;
    }
};

constexpr u8 kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                            41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                            30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// The inverse transform, rows then columns.
void idct(const float in[64], u8* out, size_t stride) {
    static const auto c = [] {
        std::array<float, 64> t{};
        for (int x = 0; x < 8; ++x)
            for (int u = 0; u < 8; ++u) t[size_t(x * 8 + u)] = float((u ? 1.0 : std::sqrt(0.5)) * std::cos((2 * x + 1) * u * 3.14159265358979 / 16) / 2);
        return t;
    }();
    float mid[64];
    for (int v = 0; v < 8; ++v) {
        const float* r = in + v * 8;
        for (int x = 0; x < 8; ++x) {
            float s = 0;
            for (int u = 0; u < 8; ++u) s += c[size_t(x * 8 + u)] * r[u];
            mid[v * 8 + x] = s;
        }
    }
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 8; ++y) {
            float s = 0;
            for (int v = 0; v < 8; ++v) s += c[size_t(y * 8 + v)] * mid[v * 8 + x];
            out[size_t(y) * stride + size_t(x)] = u8(std::clamp(s + 128.5f, 0.0f, 255.0f));
        }
}

struct Component {
    u8 id = 0;
    int h = 1, v = 1, quant = 0, dc = 0, ac = 0;
    s32 pred = 0;
    size_t stride = 0, rows = 0;
    std::vector<u8> samples;
};

u32 be16(ByteSpan d, size_t o) { return u32(d[o]) << 8 | d[o + 1]; }
float lef(ByteSpan d, size_t o) {
    float f;
    std::memcpy(&f, d.data() + o, 4);
    return f;
}

} // namespace

JpegPicture decode_jpeg(ByteSpan d) {
    if (d.size() < 4 || d[0] != 0xFF || d[1] != 0xD8) fail("no picture");
    JpegPicture out;
    std::array<std::array<float, 64>, 4> quant{};
    std::array<Huffman, 4> dc, ac;
    std::vector<Component> comps;
    int interval = 0, hmax = 1, vmax = 1;
    for (size_t at = 2;;) {
        while (at < d.size() && d[at] != 0xFF) ++at;
        while (at < d.size() && d[at] == 0xFF) ++at;
        if (at >= d.size()) fail("no scan");
        const u8 marker = d[at++];
        if (marker == 0xD9) fail("no scan");
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;
        if (at + 2 > d.size()) fail("cut short");
        const size_t len = be16(d, at);
        if (len < 2 || at + len > d.size()) fail("cut short");
        const ByteSpan seg = d.subspan(at + 2, len - 2);
        at += len;
        if (marker == 0xDB) {
            for (size_t o = 0; o < seg.size();) {
                const u8 pq = seg[o] >> 4, tq = seg[o] & 3;
                const size_t size = pq ? 128 : 64;
                if (o + 1 + size > seg.size()) fail("quantization table cut short");
                for (size_t i = 0; i < 64; ++i) quant[tq][kZigzag[i]] = float(pq ? be16(seg, o + 1 + 2 * i) : seg[o + 1 + i]);
                o += 1 + size;
            }
        } else if (marker == 0xC4) {
            for (size_t o = 0; o + 17 <= seg.size();) {
                Huffman& h = (seg[o] >> 4 ? ac : dc)[seg[o] & 3];
                h.build(seg.subspan(o + 1));
                o += 17 + h.symbols.size();
            }
        } else if (marker == 0xC0 || marker == 0xC1) {
            if (seg.size() < 6 || seg[0] != 8) fail("not 8 bits");
            out.height = int(be16(seg, 1)), out.width = int(be16(seg, 3));
            const size_t n = seg[5];
            if ((n != 1 && n != 3) || seg.size() < 6 + 3 * n) fail("unknown components");
            if (!out.width || !out.height || out.width > 8192 || out.height > 8192) fail("unknown size");
            for (size_t i = 0; i < n; ++i) {
                Component c;
                c.id = seg[6 + 3 * i], c.h = seg[7 + 3 * i] >> 4, c.v = seg[7 + 3 * i] & 15, c.quant = seg[8 + 3 * i] & 3;
                if (c.h < 1 || c.h > 4 || c.v < 1 || c.v > 4) fail("unknown sampling");
                hmax = std::max(hmax, c.h), vmax = std::max(vmax, c.v);
                comps.push_back(c);
            }
        } else if (marker >= 0xC2 && marker <= 0xCF && marker != 0xC8 && marker != 0xCC) {
            fail("not a baseline picture");
        } else if (marker == 0xDD) {
            if (seg.size() >= 2) interval = int(be16(seg, 0));
        } else if (marker == 0xE8) {
            // "G4PI PDI\0", u32 1, u32 type (3: the focus), six floats.
            if (seg.size() >= 41 && std::memcmp(seg.data(), "G4PI PDI", 9) == 0 && seg[13] == 3)
                out.focus = JpegPicture::Focus{lef(seg, 17), lef(seg, 21), lef(seg, 25), lef(seg, 29), lef(seg, 33), lef(seg, 37)};
        } else if (marker == 0xDA) {
            if (comps.empty() || seg.empty() || seg[0] != comps.size() || seg.size() < 1 + 2 * comps.size()) fail("unknown scan");
            for (size_t i = 0; i < comps.size(); ++i)
                for (Component& c : comps)
                    if (c.id == seg[1 + 2 * i]) c.dc = seg[2 + 2 * i] >> 4 & 3, c.ac = seg[2 + 2 * i] & 3;
            if (comps.size() == 1) comps[0].h = comps[0].v = hmax = vmax = 1;
            const int mw = hmax * 8, mh = vmax * 8, across = (out.width + mw - 1) / mw, down = (out.height + mh - 1) / mh;
            for (Component& c : comps) {
                if (!dc[size_t(c.dc)].set || !ac[size_t(c.ac)].set) fail("no Huffman table");
                c.stride = size_t(across * c.h * 8), c.rows = size_t(down * c.v * 8);
                c.samples.assign(c.stride * c.rows, 0);
            }
            Bits bits{d, at};
            for (int mcu = 0; mcu < across * down; ++mcu) {
                if (interval && mcu && mcu % interval == 0) {
                    bits.restart();
                    for (Component& c : comps) c.pred = 0;
                }
                for (Component& c : comps)
                    for (int by = 0; by < c.v; ++by)
                        for (int bx = 0; bx < c.h; ++bx) {
                            float block[64] = {};
                            const std::array<float, 64>& q = quant[size_t(c.quant)];
                            c.pred += bits.extend(bits.symbol(dc[size_t(c.dc)]) & 15);
                            block[0] = float(c.pred) * q[0];
                            for (int k = 1; k < 64;) {
                                const u8 rs = bits.symbol(ac[size_t(c.ac)]);
                                const int run = rs >> 4, size = rs & 15;
                                if (!size) {
                                    if (run != 15) break;
                                    k += 16;
                                    continue;
                                }
                                k += run;
                                if (k > 63) break;
                                const u8 z = kZigzag[k++];
                                block[z] = float(bits.extend(size)) * q[z];
                            }
                            idct(block, &c.samples[size_t(mcu / across * c.v + by) * 8 * c.stride + size_t(mcu % across * c.h + bx) * 8], c.stride);
                        }
            }
            break;
        }
    }
    out.rgba.resize(size_t(out.width) * size_t(out.height));
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            float s[3] = {0, 128, 128};
            for (size_t i = 0; i < comps.size(); ++i) {
                const Component& c = comps[i];
                s[i] = c.samples[size_t(y * c.v / vmax) * c.stride + size_t(x * c.h / hmax)];
            }
            const float cb = s[1] - 128, cr = s[2] - 128;
            const u32 r = u32(std::clamp(s[0] + 1.402f * cr + 0.5f, 0.0f, 255.0f)), g = u32(std::clamp(s[0] - 0.344136f * cb - 0.714136f * cr + 0.5f, 0.0f, 255.0f)),
                      b = u32(std::clamp(s[0] + 1.772f * cb + 0.5f, 0.0f, 255.0f));
            out.rgba[size_t(y) * size_t(out.width) + size_t(x)] = r | g << 8 | b << 16 | 0xFF000000u;
        }
    return out;
}

} // namespace gt4r::menu
