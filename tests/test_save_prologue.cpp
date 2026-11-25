// Gran Turismo 4 Prologue's saved game (save/prologue_data.h): a new game's payload, the state written and read
// back, and on the disc the memory card pages saving the license record and the name and loading them again.

#include "menu_run_flow.h"

#include "gt4r/menu/prologue_license.h"
#include "gt4r/save/codec.h"
#include "gt4r/save/prologue_data.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using script::Value;

namespace {

u32 word(ByteSpan b, size_t o) { return u32(b[o]) | u32(b[o + 1]) << 8 | u32(b[o + 2]) << 16 | u32(b[o + 3]) << 24; }

Disc* prologue_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::Prologue) return d.get();
    return nullptr;
}

} // namespace

TEST(save_prologue_payload_written_and_read) {
    const Bytes fresh = save::new_prologue_payload();
    CHECK(fresh.size() == save::kProloguePayloadSize && save::valid_prologue(fresh));
    CHECK(word(fresh, 0) == 0xFF60BAD0 && word(fresh, 4) == u32(-s32(0x1E790)));
    // The rankings' empty entries and the options' block size.
    CHECK(word(fresh, save::kPrologueLicense + 0x20) == 359'999'999 && fresh[save::kPrologueLicense + 0x34] == '-');
    CHECK(word(fresh, save::kPrologueOptions) == 0x6B8);
    menu::GameState s;
    s.username = "ABCDEFGHIJKLMNOPQ"; // longer than the field: 15 letters kept
    s.prologue_license.assign(fresh.begin() + save::kPrologueLicense,
                              fresh.begin() + save::kPrologueLicense + save::kPrologueLicenseUnits * save::kPrologueLicenseUnit);
    s.prologue_license[4] = 3;
    const Bytes p = save::write_prologue(s);
    CHECK(save::valid_prologue(p) && p[save::kPrologueLicense + 4] == 3);
    menu::GameState back;
    CHECK(save::read_prologue(p, back));
    CHECK(back.username == "ABCDEFGHIJKLMNO" && back.prologue_license == s.prologue_license && back.saved_image == p);
    // Written again from what was read: the same bytes.
    CHECK(save::write_prologue(back) == p);
    Bytes broken = p;
    broken[0x100] ^= 1;
    CHECK(!save::valid_prologue(broken) && !save::read_prologue(broken, back));
}

TEST(disc_prologue_game_saved_and_loaded) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r-test-prologue-save";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / "memorycard" / "BISCPS-15055GAMEDATA" / "BISCPS-15055GAMEDATA";
    auto call = [](script::Vm& vm, const Value& o, const char* m, std::vector<Value> a) { return vm.call(vm.attribute(o, m), a); };
    {
        Flow f(*disc, "JP", dir.string());
        f.rt->boot();
        script::Vm& vm = f.rt->vm();
        const Value lic = vm.attribute(vm.lookup("game"), "license");
        CHECK(lic.p != nullptr);
        if (!lic.p) return;
        // A fresh record is the program's new game's (gt4r_diff prologue-save checks that payload), but each unit's
        // category (+0x00), which the main script writes at power on.
        const std::vector<u8> units = menu::prologue_license_units(*vm.lookup("game").as<menu::Object>());
        const Bytes fresh = save::new_prologue_payload();
        size_t differ = 0;
        for (size_t i = 0; i < units.size(); ++i)
            if (i % save::kPrologueLicenseUnit >= 4 && units[i] != fresh[save::kPrologueLicense + i] && !differ++)
                std::printf("  fresh units differ first at +0x%zX (unit %zu): ours %02X, the program's new game %02X\n", i, i / save::kPrologueLicenseUnit, units[i],
                            fresh[save::kPrologueLicense + i]);
        CHECK(units.size() == save::kPrologueLicenseUnits * save::kPrologueLicenseUnit && differ == 0);
        call(vm, lic, "recordResult", {Value::string("l0p0001"), Value::string("gold"), Value::integer(107500), Value::integer(100), Value::string("GTR")});
        call(vm, lic, "setDefaultName", {Value::string("GTR")});
        // Back at the title after the test: its present (the script's present list: a passed test's car) shows first.
        vm.member(vm.lookup("game"), "race_code")->set(vm, Value::string("l0p0001"));
        f.rt->start_project("prologue");
        f.rt->settle();
        std::printf("  after the test: %s\n", f.page().c_str());
        CHECK(f.page() == "PrizeRoot");
        // The title's Save: the memory card project's GameSave page, Yes.
        f.rt->game().next_menu_arg = "save_game";
        f.rt->start_project("memcard");
        f.rt->settle();
        std::printf("  save page: %s [%s]\n", f.page().c_str(), f.focus().c_str());
        CHECK(f.page() == "GameSave");
        CHECK(f.focus_on("Yes"));
        f.press({menu::Key::Accept});
        for (int i = 0; i < 120; ++i) f.rt->update();
        f.rt->settle();
        std::printf("  after Yes: %s [%s]\n", f.page().c_str(), f.focus().c_str());
        CHECK(std::filesystem::exists(file) && std::filesystem::exists(file.parent_path() / "icon.sys") &&
              std::filesystem::exists(file.parent_path() / "icon0.ico") && !std::filesystem::exists(file.parent_path() / "garage"));
        std::ifstream in(file, std::ios::binary);
        const Bytes saved((std::istreambuf_iterator<char>(in)), {});
        CHECK(save::valid_prologue(saved));
        if (save::valid_prologue(saved)) {
            CHECK(std::string(reinterpret_cast<const char*>(&saved[save::kPrologueName])) == "GTR");
            CHECK(word(saved, save::kPrologueLicense + 4) == 3 && word(saved, save::kPrologueLicense + 0x20) == 107500);
        }
        CHECK(f.clean());
    }
    {
        // The next start: the title's Load Game (the GameLoad page, Yes) brings the record and the name back.
        Flow f(*disc, "JP", dir.string());
        f.rt->boot();
        script::Vm& vm = f.rt->vm();
        const Value lic = vm.attribute(vm.lookup("game"), "license");
        if (!lic.p) return;
        f.rt->game().next_menu_arg = "load_game";
        f.rt->start_project("memcard");
        f.rt->settle();
        std::printf("  load page: %s [%s]\n", f.page().c_str(), f.focus().c_str());
        CHECK(f.page() == "GameLoad");
        CHECK(f.focus_on("Yes"));
        f.press({menu::Key::Accept});
        for (int i = 0; i < 120; ++i) f.rt->update();
        f.rt->settle();
        const Value unit = call(vm, lic, "getUnit", {Value::string("l0p0001")});
        CHECK(unit.p && call(vm, unit, "isCompletePassed", {}).truthy());
        CHECK(unit.p && call(vm, unit, "getBestTime", {Value::integer(0)}).as_string() == "1'47.500");
        CHECK(call(vm, lic, "getDefaultName", {}).as_string() == "GTR");
        // The title: the loaded record's present counted as given (refreshPresent), the school's ring.
        f.rt->start_project("prologue");
        f.rt->settle();
        CHECK(f.page() == "SchoolRoot");
        CHECK(f.clean());
    }
}
