// A mod car online (net/mod_car_package.h, the lobby's packages): two machines on the loopback, the first with a mod car,
// the second without it; the second fetches the car's package from the first, checks it, installs it, and then builds the
// same car (the same digest).

#include "test_framework.h"

#include "gt4r/formats/db/mod_cars.h"
#include "gt4r/net/car_choice.h"
#include "gt4r/net/lobby.h"
#include "gt4r/net/mod_car_package.h"
#include "gt4r/sim/program.h"

#include "base/file.h"
#include "base/sha256.h"
#include "net_ports.h"

#include <chrono>
#include <filesystem>
#include <thread>

using namespace gt4r;
using gt3r::net::Session;

namespace {

namespace fs = std::filesystem;

struct Machine {
    Session session;
    net::Lobby lobby{session};
    Machine() { session.set_disc({"gt4", "another game"}); }
};

bool pump(std::vector<Machine*> all, const std::function<bool()>& done, int ms = 8000) {
    for (int t = 0; t < ms; t += 2) {
        for (Machine* m : all) m->lobby.poll();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

Disc* gt4_disc() {
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4) return d;
    return nullptr;
}

std::unique_ptr<db::SpecDb> open_db(Disc& disc, const std::string& list) {
    gt3r::Volume* vol = &disc.volume();
    return std::make_unique<db::SpecDb>(
        [vol, list](const std::string& path) -> std::optional<Bytes> {
            if (path == "mods/.cars") return Bytes(list.begin(), list.end());
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        },
        [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
}

void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    gt3r::write_host_file(p.string(), ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()));
}

// A picture of w x h, as PNG.
Bytes picture(u32 w, u32 h);

} // namespace

#include "formats/model/gltf_export.h"
#include "formats/png_writer.h"

namespace {
Bytes picture(u32 w, u32 h) {
    std::vector<u32> rgba(size_t(w) * h, 0xFF2040C0u);
    return gt3r::encode_png(w, h, rgba);
}
} // namespace

TEST(mod_car_package) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("no Gran Turismo 4 disc");
    const fs::path root = fs::temp_directory_path() / "gt4r_test_mod_car_package";
    fs::remove_all(root);
    const fs::path mod = root / "owner" / "mods" / "fast";
    write(mod / "cars" / "gt4r_net_coupe_26.ini", "[car]\nlabel = gt4r_net_coupe_26\nname = Net Coupe\ndonor = nsx_r_02\nprice = 50000\n[body]\nweight_kg = 1150\n[powertrain]\npower_kw = 240\n[dealer]\nthumb = images/thumb.png\n");
    const Bytes thumb = picture(102, 54);
    fs::create_directories(mod / "images");
    gt3r::write_host_file((mod / "images" / "thumb.png").string(), thumb);
    const std::string list = "fast\t" + (mod / "cars" / "gt4r_net_coupe_26.ini").string() + "\n";
    std::unique_ptr<db::SpecDb> owner = open_db(*disc, list), other = open_db(*disc, "");
    CHECK(owner->mod_cars().def("gt4r_net_coupe_26"));

    // The package: the car file and its picture.
    std::string why;
    const std::optional<Bytes> blob = net::build_package(*owner, "gt4r_net_coupe_26", &why);
    CHECK(blob);
    if (!blob) return std::printf("  %s\n", why.c_str()), void();
    const std::optional<gt3r::game::ModPackage> p = gt3r::game::parse_mod_package(*blob, &why);
    CHECK(p && p->files.size() == 2 && net::package_donor(*p) == "nsx_r_02");
    CHECK(p && net::validate_package(*p, *other, "gt4r_net_coupe_26", &why));
    if (!p) return;
    // What is refused: another label, a file it does not name, a donor this disc lacks, values no car has.
    CHECK(!net::validate_package(*p, *other, "gt4r_other", &why));
    gt3r::game::ModPackage q = *p;
    q.files.push_back({"images/extra.png", thumb});
    CHECK(!net::validate_package(q, *other, "gt4r_net_coupe_26", &why) && why.find("extra") != std::string::npos);
    auto with_ini = [&](const std::string& text) {
        gt3r::game::ModPackage r = *p;
        for (auto& f : r.files)
            if (f.path.find(".ini") != std::string::npos) f.data.assign(text.begin(), text.end());
        r.files.resize(1); // (no picture named)
        return r;
    };
    CHECK(!net::validate_package(with_ini("[car]\nlabel = gt4r_net_coupe_26\nname = N\ndonor = no_car\n"), *other, "gt4r_net_coupe_26", &why));
    CHECK(!net::validate_package(with_ini("[car]\nlabel = gt4r_net_coupe_26\nname = N\ndonor = nsx_r_02\n[body]\nweight_kg = 99999\n"), *other, "gt4r_net_coupe_26", &why));
    CHECK(net::validate_package(with_ini("[car]\nlabel = gt4r_net_coupe_26\nname = N\ndonor = nsx_r_02\n"), *other, "gt4r_net_coupe_26", &why));
    // A body of its own: carried when its file is one the loader reads.
    {
        gt3r::scene::Model tri;
        gt3r::scene::Mesh mesh;
        for (int i = 0; i < 3; ++i) {
            gt3r::scene::Vertex v;
            v.pos[0] = float(i == 1), v.pos[1] = float(i == 2), v.normal[2] = 1;
            mesh.vertices.push_back(v), mesh.indices.push_back(u32(i));
        }
        gt3r::scene::Primitive prim;
        prim.index_count = 3;
        mesh.primitives.push_back(prim);
        tri.meshes.push_back(mesh);
        gt3r::game::ModPackage r = with_ini("[car]\nlabel = gt4r_net_coupe_26\nname = N\ndonor = nsx_r_02\n[model]\nfile = models/body.glb\n");
        r.files.push_back({"models/body.glb", gt3r::export_glb(tri)});
        CHECK(net::validate_package(r, *other, "gt4r_net_coupe_26", &why));
        r.files.back().data.resize(40); // (not a whole file)
        CHECK(!net::validate_package(r, *other, "gt4r_net_coupe_26", &why) && why.find("body.glb") != std::string::npos);
    }

    // Through the lobby: the second machine fetches, checks and installs it; its database read again builds the same car.
    const sim::Program program = sim::Program::load(*disc);
    gt3r::Volume* vol = &disc->volume();
    const sim::FileReader read = [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    };
    net::CarBuilder owner_cars(*owner, program, read);
    Session::set_advertise_lan(false);
    Machine a, b;
    std::string error;
    CHECK(test::on_free_port([&](u16 port) { return a.session.host(port, "One", "", 0, &error); }));
    a.lobby.begin("One");
    CHECK(b.session.join("127.0.0.1:" + std::to_string(a.session.port()), "Two", "", 0, &error));
    b.lobby.begin("Two");
    CHECK(pump({&a, &b}, [&] { return b.lobby.active() && a.lobby.count() == 2 && b.lobby.heard(0); }));
    a.lobby.describe_car = [&](net::CarChoice& c) {
        const std::string hash = gt3r::sha256_hex(*blob);
        a.session.offer_package(hash, *blob);
        c.package = hash, c.package_size = u32(blob->size()), c.donor = "nsx_r_02";
    };
    std::string installed;
    const fs::path mods = root / "other" / "mods";
    fs::create_directories(mods);
    b.lobby.wants_package = [&](const net::CarChoice& c) { return !other->labels("GENERIC_CAR")->id(c.label) && other->labels("GENERIC_CAR")->id(c.donor); };
    b.lobby.on_package = [&](const std::string& hash, Bytes got, const net::CarChoice& c, const std::string& from) -> std::string {
        std::string w;
        const std::optional<gt3r::game::ModPackage> pk = gt3r::game::parse_mod_package(got, &w);
        if (!pk || !net::validate_package(*pk, *other, c.label, &w)) return "refused: " + w;
        const std::optional<std::string> dir = gt3r::game::install_mod_package(*pk, hash, mods.string(), "Net Coupe", from, &w);
        if (!dir) return "not written: " + w;
        installed = *dir;
        return {};
    };
    net::CarChoice choice;
    choice.label = "gt4r_net_coupe_26", choice.paint = 0;
    choice.digest = owner_cars.digest(choice);
    CHECK(choice.digest != 0);
    a.lobby.change([&](net::Member& m) { m.choice = choice, m.car = choice.label; });
    CHECK(pump({&a, &b}, [&] { return !installed.empty(); }, 15000));
    CHECK(!installed.empty() && fs::exists(fs::path(installed) / "cars" / "gt4r_net_coupe_26.ini") && fs::exists(fs::path(installed) / "images" / "thumb.png"));
    const net::Member& seen = b.lobby.member(0);
    CHECK(seen.choice && seen.choice->package == gt3r::sha256_hex(*blob) && seen.choice->donor == "nsx_r_02");
    if (!installed.empty() && seen.choice) {
        std::unique_ptr<db::SpecDb> again = open_db(*disc, "dl\t" + (fs::path(installed) / "cars" / "gt4r_net_coupe_26.ini").string() + "\n");
        net::CarBuilder cars(*again, program, read);
        CHECK(cars.problem(*seen.choice).empty());
        CHECK(cars.digest(*seen.choice) == choice.digest);
    }
    a.lobby.end(), b.lobby.end();
    fs::remove_all(root);
}
