// What the player is told when the file chosen as the disc cannot be played: a missing path, a file
// that is no disc image, and (with GT4R_WRONG_DISC naming one) another game's disc image.

#include "test_framework.h"

#include "base/file.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace gt4r;

namespace {

std::string open_message(const std::string& path) {
    try {
        Disc::open(path);
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// No internals reach the player: file names of the disc, format words.
bool plain(const std::string& s) { return !contains(s, "CORE.") && !contains(s, ".VOL") && !contains(s, "ISO9660") && !contains(s, "volume"); }

} // namespace

TEST(disc_message_missing_file) {
    const std::string path = (std::filesystem::temp_directory_path() / "gt4r-no-such-folder" / "Gran Turismo 4.iso").string();
    const std::string m = open_message(path);
    CHECK(contains(m, "is not there"));
    CHECK(contains(m, path.c_str()));
    CHECK(contains(m, "moved or renamed"));
}

TEST(disc_message_not_a_disc_image) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r-disc-messages";
    std::filesystem::create_directories(dir);
    for (const std::uintmax_t size : {std::uintmax_t(40), std::uintmax_t(1) << 20}) { // short, and long enough to be read as one
        const std::filesystem::path file = dir / "not a disc.iso";
        {
            std::ofstream out(file, std::ios::binary);
            for (std::uintmax_t i = 0; i < size; ++i) out.put(char('a' + i % 26));
        }
        const std::string m = open_message(file.string());
        CHECK(contains(m, "not a disc image"));
        CHECK(contains(m, ".iso or .chd"));
        CHECK(plain(m));
    }
    std::filesystem::remove_all(dir);
}

TEST(disc_message_other_game) {
    const char* path = std::getenv("GT4R_WRONG_DISC"); // e.g. a Gran Turismo 3 A-Spec image
    if (!path || !gt3r::host_path_exists(path)) SKIP("GT4R_WRONG_DISC not set");
    const std::string m = open_message(path);
    CHECK(contains(m, "Gran Turismo 3 A-Spec or Gran Turismo Concept"));
    CHECK(contains(m, "Gran Turismo 4"));
    CHECK(plain(m));
}
