// The car database's files on made-up data, then on the discs of GT4R_DISC.

#include "test_framework.h"

#include "gt4r/formats/db/specdb.h"

#include <cstring>

using namespace gt4r;

namespace {

void put32(Bytes& b, size_t o, u32 v) { std::memcpy(b.data() + o, &v, 4); }

// Bits appended from the low end of each byte.
struct BitWriter {
    Bytes b;
    size_t bits = 0;
    void put(u32 v, u32 n) {
        for (u32 i = 0; i < n; ++i, ++bits) {
            if (bits / 8 >= b.size()) b.push_back(0);
            b[bits / 8] |= u8(((v >> i) & 1) << (bits & 7));
        }
    }
};

// A table of 4-byte rows whose dictionary gives every byte its own 8-bit code, except 0xEE, which
// has the 9-bit code 0x1EE (0xEE's 8-bit place says "longer").
Bytes make_table() {
    struct Row {
        u32 id;
        Bytes codes; // the bytes the codes stand for, the row's head first
    };
    const std::vector<Row> rows = {
        {5, {0x01}},                                   // kind 0: plain row 1
        {9, {0x40, 0xDE, 0xAD, 0xEE, 0x07}},           // kind 1: a code per byte
        {9, {0x80, 0x05, 0x11, 0xEE}},                 // kind 2: plain row 0 with bytes 0 and 2 changed
        {12, {0x00}},                                  // kind 0: plain row 0
    };
    Bytes f(0x10 + 8 * rows.size(), 0);
    std::memcpy(f.data(), "GTDB", 4);
    put32(f, 4, 0x80001), put32(f, 8, u32(rows.size())), put32(f, 12, 4);
    const size_t dict = f.size();
    f.resize(dict + 8 + 512 + 8, 0);
    put32(f, dict, 8 + 512 + 8), put32(f, dict + 4, 1);
    for (u32 i = 0; i < 256; ++i) f[dict + 8 + 2 * i] = u8(i), f[dict + 9 + 2 * i] = i == 0xEE ? 0 : 8;
    f[dict + 520] = 9, f[dict + 521] = 0xEE;
    put32(f, dict + 524, 0x1EE);
    const size_t plain = f.size();
    const u8 plain_rows[2][4] = {{0xA0, 0xA1, 0xA2, 0xA3}, {0xB0, 0xB1, 0xB2, 0xB3}};
    f.resize(plain + 8 + 8, 0);
    put32(f, plain, 16), put32(f, plain + 4, 2);
    std::memcpy(f.data() + plain + 8, plain_rows, 8);
    const size_t stream = f.size();
    for (size_t r = 0; r < rows.size(); ++r) {
        put32(f, 0x10 + 8 * r, rows[r].id), put32(f, 0x10 + 8 * r + 4, u32(f.size() - stream));
        f.push_back(u8(rows[r].codes.size()));
        BitWriter w;
        for (u8 c : rows[r].codes) c == 0xEE ? w.put(0x1EE, 9) : w.put(c, 8);
        f.insert(f.end(), w.b.begin(), w.b.end());
    }
    return f;
}

} // namespace

TEST(specdb_table_unpacks) {
    const db::Table t = db::Table::parse(make_table());
    CHECK(t.size() == 4 && t.row_size() == 4 && t.ids() == (std::vector<u32>{5, 9, 9, 12}));
    auto is = [&](size_t i, std::initializer_list<u8> want) {
        const ByteSpan r = t.row_at(i);
        return std::equal(r.begin(), r.end(), want.begin(), want.end());
    };
    CHECK(is(0, {0xB0, 0xB1, 0xB2, 0xB3}));
    CHECK(is(1, {0xDE, 0xAD, 0xEE, 0x07}));
    CHECK(is(2, {0x11, 0xA1, 0xEE, 0xA3}));
    CHECK(is(3, {0xA0, 0xA1, 0xA2, 0xA3}));
    size_t first = 0;
    CHECK(t.rows(9, first) == 2 && first == 1 && t.rows(7, first) == 0);
    CHECK(t.row(12).size() == 4 && t.row(13).empty());
    // A table without rows, and a truncated one.
    Bytes empty(0x18, 0);
    std::memcpy(empty.data(), "GTDB", 4);
    put32(empty, 12, 65);
    CHECK(db::Table::parse(empty).size() == 0);
    bool threw = false;
    try {
        const Bytes f = make_table();
        db::Table::parse(ByteSpan(f).first(f.size() - 9));
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(specdb_labels_and_strings) {
    Bytes l(0x10 + 16, 0);
    std::memcpy(l.data(), "GTID", 4);
    put32(l, 4, 2), put32(l, 12, 7);
    const char names[] = "\x06\x00" "ab_01\0" "\x03\x00" "zz\0";
    put32(l, 0x10, 0), put32(l, 0x14, 40), put32(l, 0x18, 8), put32(l, 0x1C, 3);
    l.insert(l.end(), names, names + sizeof names - 1);
    const db::Labels labels = db::Labels::parse(l);
    CHECK(labels.table_id == 7 && labels.entries.size() == 2);
    CHECK(labels.id("ab_01") == 40u && labels.id("zz") == 3u && !labels.id("no"));
    CHECK(labels.label(3) && *labels.label(3) == "zz" && !labels.label(4));

    Bytes s(0x10 + 8, 0);
    std::memcpy(s.data(), "GTST", 4);
    put32(s, 4, 2);
    const char pool[] = "\x07\x00" "Amalfi\0" "\x00\x00";
    put32(s, 0x10, 0), put32(s, 0x14, 9);
    s.insert(s.end(), pool, pool + sizeof pool - 1);
    const db::Strings strings = db::Strings::parse(s);
    CHECK(strings.strings.size() == 2 && strings.at(0) == "Amalfi" && strings.at(1).empty() && strings.at(9).empty());
}

TEST(disc_database_reads) {
    if (test::reference_discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::reference_discs()) {
        if (disc->info().game != Game::GT4) continue;
        gt3r::Volume* vol = &disc->volume();
        db::SpecDb sdb(
            [vol](const std::string& path) -> std::optional<Bytes> {
                const gt3r::Volume::Entry* e = vol->find(path);
                return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
            },
            [vol](const std::string& dir) { return vol->list_dir(dir); });
        size_t rows = 0;
        for (const std::string& name : sdb.table_names()) {
            const db::Table* t = sdb.table(name);
            CHECK(t != nullptr);
            if (t) rows += t->size();
        }
        CHECK(sdb.table_names().size() == 79 && rows == 102767);
        const std::vector<db::CarEntry> cars = sdb.cars();
        CHECK(cars.size() == 722);
        size_t with_model = 0;
        bool cobra = false;
        for (const db::CarEntry& c : cars) {
            with_model += !c.model.empty() && c.paints > 0 && vol->find("car/lod/" + c.model) && vol->find("car/menu/" + c.model);
            cobra |= c.label == "ac_427_sc_66" && c.model == "ac__0014" && c.name == "AC Cars 427 S/C `66" && c.paints == 16;
        }
        CHECK(with_model == cars.size() && cobra);
        bool amalfi = false;
        for (const db::CourseEntry& c : sdb.courses()) amalfi |= c.label == "capri" && c.name == "Amalfi";
        CHECK(amalfi);
    }
}
