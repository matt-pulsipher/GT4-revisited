// The saved game: the scrambling of its files, the game data's blocks written and read back, a
// card folder saved, loaded and checked (save/).

#include "test_framework.h"

#include "gt4r/save/codec.h"
#include "gt4r/save/game_data.h"
#include "gt4r/save/memory_card.h"

#include <cstring>
#include <filesystem>
#include <fstream>

using namespace gt4r;

namespace {

// Made-up codes: a label "car7" is row 7 of table 0, "course3" row 3 of table 0x22.
save::Codes made_up_codes() {
    return {[](u32 table, const std::string& label) -> u64 {
                const size_t digits = label.find_first_of("0123456789");
                if (digits == std::string::npos) return ~u64(0);
                const std::string kind = label.substr(0, digits);
                if ((table == 0 && kind == "car") || (table == 0x22 && kind == "course") || (table == 29 && kind == "wheel"))
                    return u64(table) << 32 | u64(std::stoul(label.substr(digits)));
                return ~u64(0);
            },
            [](u64 code) -> std::string {
                const u32 table = u32(code >> 32);
                if (code == ~u64(0)) return {};
                return (table == 0 ? "car" : table == 0x22 ? "course" : table == 29 ? "wheel" : "x") + std::to_string(u32(code));
            }};
}

menu::GameState a_game() {
    menu::GameState s;
    s.username = "Driver", s.money = 1'234'567, s.score = 42, s.total_prize_money = 98'000, s.total_prize_cars = 3;
    s.rank_counts = {1, 5, 4, 3, 2, 1, 0};
    for (int i = 0; i < 9; ++i) {
        menu::DayEvent e;
        e.type = u8(i % 9), e.b1 = u8(i), e.word = u32(60'000 + i), e.code = u64(0x23) << 32 | u32(100 + i);
        s.put_day(e);
    }
    menu::GarageEntry a, b;
    a.car = "car12", a.paint = 3, a.power = 28'000, a.weight = 1'180, a.day = s.today, a.odometer = 15'000, a.ride_rank = 0;
    a.record.emplace();
    a.record->set_odometer(123'456), a.record->set_active_sheet(1), a.record->set_owned(17, true), a.record->sheet(2)[0x12F] = 7;
    b.car = "car40", b.paint = 1, b.day = s.today - 3, b.ride_rank = 0x3FF;
    s.add_car(a), s.add_car(b);
    s.garage[1].used = false; // sold: the place keeps its car
    s.riding_car = 0, s.garage_key = 0xC0FFEE11, s.garage_serial = 5;
    s.race_units[10] = 0x00051231;
    s.available_cars = {"car12", "car3"}, s.available_courses = {"course9"};
    s.favorite_cars = {"car40", "car12"};
    s.presents[3] = 0x80000001;
    s.suspended_racecode = "pr_gtworld_1000", s.suspended = 1, s.suspended_session = 2;
    s.options["race_laps"] = s32(5), s.options["game_zone"] = std::string("US");
    s.license_record[0x2C * 2] = 0x12;
    return s;
}

} // namespace

// The pieces of the scrambling: the standard CRC-32 and Mersenne Twister, the generator's
// floats, the round trips.
TEST(save_codec_pieces) {
    const char* digits = "123456789";
    CHECK(save::crc32(ByteSpan(reinterpret_cast<const u8*>(digits), 9)) == 0xCBF43926u);
    save::Mt19937 mt(5489);
    CHECK(mt.next() == 3499211612u && mt.next() == 581869302u);
    // below(n): the draw's top 24 significant bits as a fraction of 2^32, times n.
    save::Mt19937 a(1), b(1);
    for (int i = 0; i < 100; ++i) {
        const u32 v = a.next();
        int bits = 32;
        while (bits && !(v >> (bits - 1))) --bits;
        const u64 top = bits > 24 ? (u64(v) >> (bits - 24)) << (bits - 24) : v;
        CHECK(b.below(1000) == u32(top * 1000 >> 32));
    }
    save::Lcg lcg(0);
    CHECK(lcg.next() == (17u ^ (17u << 16)));
    // A file of a payload and back; one bit changed fails the CRC.
    Bytes payload(1000);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = u8(i * 7 + 3);
    const Bytes file = save::scramble(payload, 0, 0x12345678);
    CHECK(file.size() == 0x400 && std::memcmp(file.data() + 8, payload.data(), 16) != 0);
    const std::optional<Bytes> back = save::unscramble(file, 0);
    CHECK(back && back->size() == 0x3F8 && std::memcmp(back->data(), payload.data(), payload.size()) == 0);
    Bytes broken = file;
    broken[100] ^= 1;
    CHECK(!save::unscramble(broken, 0));
    CHECK(!save::unscramble(file, 1)); // another key
    // The garage file's header and a slot.
    u8 h[0x40] = {};
    for (int i = 0; i < 0x40; ++i) h[i] = u8(i * 3);
    u8 plain[0x40];
    std::memcpy(plain, h, 0x40);
    save::encode_header(h, 0xABCD1234);
    CHECK(std::memcmp(h, plain, 0x40) != 0);
    save::decode_header(h, 0xABCD1234);
    CHECK(std::memcmp(h, plain, 0x40) == 0);
    Bytes record(0x4B8);
    for (size_t i = 0; i < record.size(); ++i) record[i] = u8(i);
    const Bytes slot = save::encode_slot(record, 0xABCD1234, 7, 0x55AA);
    const std::optional<Bytes> rec = save::decode_slot(slot, 0xABCD1234, 7);
    CHECK(slot.size() == 0x500 && rec && std::memcmp(rec->data(), record.data(), record.size()) == 0);
    CHECK(!save::decode_slot(slot, 0xABCD1234, 8)); // another slot's shuffle
}

// The game data: a new game's payload is valid; a game written and read back is the same game.
TEST(save_game_data_round_trip) {
    const save::Codes codes = made_up_codes();
    const Bytes fresh = save::write_game(menu::GameState(), codes);
    CHECK(fresh.size() == save::kPayloadSize && save::valid_game(fresh));
    // The new game's blocks: today 2005-04-02, no car, the rankings empty.
    CHECK(fresh[0xB8D4] == 0xD6 && fresh[0xB8D5] == 0x6F && fresh[0xB8D6] == 0x25);
    CHECK(fresh[0xB8E0] == 0xFF && fresh[0xB8F8] == 0xFF && fresh[0xB8F9] == 0x03);
    CHECK(fresh[0x26BA8] == 15 && fresh[0x26BA9] == '-');
    const menu::GameState s = a_game();
    const Bytes payload = save::write_game(s, codes);
    CHECK(save::valid_game(payload));
    menu::GameState t;
    std::vector<int> wanted;
    CHECK(save::read_game(payload, t, codes, &wanted));
    CHECK(t.username == "Driver" && t.score == 42 && t.total_prize_money == 98'000 && t.total_prize_cars == 3 && t.rank_counts == s.rank_counts);
    CHECK(t.today == s.today && t.diary.size() == 9 && t.diary[4].b1 == 4 && t.diary[8].code == s.diary[8].code && t.diary[3].word == 60'003);
    CHECK(t.garage.size() == 2 && t.garage[0].car == "car12" && t.garage[0].paint == 3 && t.garage[0].power == 28'000 && t.garage[0].weight == 1'180);
    CHECK(t.garage[0].day == s.today && t.garage[0].odometer == 15'000 && t.garage[0].ride_rank == 0 && t.garage[0].used);
    CHECK(!t.garage[1].used && t.garage[1].car == "car40" && t.garage[1].day == s.today - 3 && !t.garage[1].record && wanted.empty());
    // The car driven's record is in the game data.
    CHECK(t.riding_car == 0 && t.garage[0].record && t.garage[0].record->odometer() == 123'456 && t.garage[0].record->active_sheet() == 1);
    CHECK(t.garage[0].record->owned(17) && !t.garage[0].record->owned(18) && t.garage[0].record->sheet(2)[0x12F] == 7);
    CHECK(t.garage_key == 0xC0FFEE11 && t.garage_serial == 5 && t.race_units[10] == 0x00051231 && t.race_units[11] == 0xFFF0);
    CHECK(t.available_cars == s.available_cars && t.available_courses == s.available_courses && t.favorite_cars == s.favorite_cars);
    CHECK(t.presents[3] == 0x80000001 && t.suspended_racecode == "pr_gtworld_1000" && t.suspended == 1 && t.suspended_session == 2);
    CHECK(t.option_int("race_laps") == 5 && t.option_text("game_zone") == "US" && t.license_record[0x58] == 0x12);
    // Written again it is the same bytes.
    CHECK(save::write_game(t, codes) == payload);
    // A championship left after two of its five races: its entrants and their points come back.
    menu::GameState c = s;
    c.series = {"jp_mazda_roadster_0500", 2, 5, {}};
    for (int k = 0; k < 6; ++k) c.series.entrants.push_back({(u64(0) << 32) | u64(100 + k), k, 20 - 3 * k});
    c.suspend_series(true);
    menu::GameState back;
    CHECK(save::read_game(save::write_game(c, codes), back, codes));
    CHECK(back.suspended == 1 && back.suspended_racecode == "jp_mazda_roadster_0500" && back.suspended_session == 2);
    CHECK(back.series.label == "jp_mazda_roadster_0500" && back.series.races_run == 2 && back.series.race_count == 5 && back.series.entrants.size() == 6);
    if (back.series.entrants.size() == 6) CHECK(back.series.entrants[5].code == 105 && back.series.entrants[5].paint == 5 && back.series.entrants[5].points == 5);
    c.suspend_series(false);
    CHECK(save::read_game(save::write_game(c, codes), back, codes) && back.suspended == 0 && back.series.entrants.empty());
    // Not a game: another length or a broken CRC.
    Bytes bad = payload;
    bad[0x5000] ^= 0x40;
    CHECK(!save::valid_game(bad) && !save::read_game(bad, t, codes));
}

// A card's folder: a new save, saved over, loaded into another game; the garage's cars come
// back from their slots and the credits from the garage file's header; a garage file of another
// save is refused.
TEST(save_card_folder) {
    const std::string dir = (std::filesystem::temp_directory_path() / "gt4r-save-test-card").string();
    std::filesystem::remove_all(dir);
    save::GameSave card(dir, made_up_codes());
    CHECK(card.connected() && card.exists() == 1);
    menu::GameState s = a_game();
    s.garage[1].used = true, s.garage[1].record.emplace(), s.garage[1].record->set_odometer(777);
    CHECK(card.save_new(s, "Game Data") == save::Result::Ok);
    CHECK(card.exists() == 0 && s.garage_serial == 2);
    CHECK(std::filesystem::file_size(card.folder() + "BASCUS-97328GAMEDATA") == 0x3A0C0 && std::filesystem::file_size(card.folder() + "garage") == 0x40 + 1000 * 0x500);
    s.money = 5'000;
    CHECK(card.save(s) == save::Result::Ok && s.garage_serial == 3);
    save::GameSave other(dir, made_up_codes());
    menu::GameState t;
    CHECK(other.load(t) == save::Result::Ok);
    CHECK(t.money == 5'000 && t.username == "Driver" && t.garage_count() == 2 && t.garage[1].record && t.garage[1].record->odometer() == 777);
    CHECK(t.garage[0].record && t.garage[0].record->odometer() == 123'456 && t.garage_serial == 2 && t.garage_key == s.garage_key);
    // Saved again from the loaded game: still one save.
    t.money = 6'000;
    CHECK(other.save(t) == save::Result::Ok);
    menu::GameState u;
    CHECK(save::GameSave(dir, made_up_codes()).load(u) == save::Result::Ok && u.money == 6'000);
    // A game data file of another save beside this garage file.
    menu::GameState stranger = a_game();
    stranger.garage_key = 0x1111;
    std::filesystem::remove_all(dir + "/x");
    save::GameSave elsewhere(dir + "/x", made_up_codes());
    CHECK(elsewhere.save_new(stranger, "Game Data") == save::Result::Ok);
    std::filesystem::copy_file(elsewhere.folder() + "BASCUS-97328GAMEDATA", card.folder() + "BASCUS-97328GAMEDATA", std::filesystem::copy_options::overwrite_existing);
    menu::GameState v;
    CHECK(save::GameSave(dir, made_up_codes()).load(v) == save::Result::DifferentMemoryCard && v.username.empty());
    std::filesystem::remove_all(dir);
}
