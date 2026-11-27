// The app's name where a player or a log reader sees it (src/gt4r/app/app_identity.h). No game data.

#include "test_framework.h"

#include "gt4r/app/app_identity.h"
#include "gt4r/net/identity.h"
#include "game/gt_save.h"
#include "net/session.h"

#include <string>

TEST(app_identity_names_the_engine) {
    gt4r::name_the_engine();
    CHECK(gt3r::app_identity().name == "GT4 Revisited");
    CHECK(gt3r::app_identity().version == GT4R_VERSION);
    CHECK(gt4r::log_file_name(false) == "gt4r.log");
    CHECK(gt4r::log_file_name(true) == "gt4r.prev.log");
    // A host of this app tells a player with an older copy to update this app, and the lobby says it in its own words.
    const std::string text = gt3r::net::protocol_mismatch(gt3r::net::kProtocolVersion - 1);
    CHECK(text.find("Please update GT4 Revisited") != std::string::npos);
    CHECK(text.find("GT3") == std::string::npos);
    CHECK(gt4r::net::refusal_text(text, false) == "The host has a newer version of the game: this copy must be updated.");
    gt3r::set_app_identity({});
}

// A portable.ini beside a macOS app bundle, not inside it: the folder holding gt4r.app from SDL's base path (the engine's
// app_bundle_folder, which user_data_dir reads).
TEST(app_folder_beside_the_bundle) {
    CHECK(gt3r::game::app_bundle_folder("/Volumes/Stick/GT4R/gt4r.app/Contents/Resources/") == "/Volumes/Stick/GT4R/");
    CHECK(gt3r::game::app_bundle_folder("/Applications/GT4 Revisited.app/Contents/Resources/") == "/Applications/");
    CHECK(gt3r::game::app_bundle_folder("/Users/me/build/").empty());        // a plain executable's folder
    CHECK(gt3r::game::app_bundle_folder("C:\\Games\\GT4R\\").empty());
    CHECK(gt3r::game::app_bundle_folder(".app/Contents/Resources/").empty());
}
