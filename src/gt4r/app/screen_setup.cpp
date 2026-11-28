// The first screen without a disc: asks for the player's own disc image, and offers the ones opened before.

#include "gt4r/app/app.h"
#include "gt4r/app/page.h"

#include "base/file.h"
#include "platform/render_scale.h"
#include "gt4r_version.h"

namespace gt4r {

namespace {

// iOS apps do not quit themselves (the app switcher closes them): Triangle does nothing on this page there.
#if defined(SDL_PLATFORM_IOS)
constexpr bool kCanQuit = false;
#else
constexpr bool kCanQuit = true;
#endif

// Where the image may be, under the page's first line (phones and tablets).
#if defined(SDL_PLATFORM_IOS)
constexpr const char* kMobileWhere = "It can be anywhere the Files app reaches: this device, iCloud Drive or a USB drive.";
#else
constexpr const char* kMobileWhere = "It can be on this device, a memory card or a USB drive.";
#endif

class SetupScreen : public Screen {
public:
    bool menu_input_any_device() const override { return true; }

    void update(App& app, double) override {
        if (!known_) {
            known_ = true;
            for (const std::string& p : app.recent_discs())
                if (p.find("://") != std::string::npos || gt3r::host_path_exists(p)) discs_.push_back(p);
        }
        build(app);
        if (kCanQuit && app.menu().pressed(PadButton::Triangle)) app.quit();
        else rows_.update(app);
        build(app);
    }

    void render(App& app, Ui& ui) override {
        ui.header("GT4 REVISITED", GT4R_VERSION);
        float y = 62;
        ui.text_center(y, "Choose your Gran Turismo 4 disc image (.iso or .chd).", colors::kText);
        ui.text_center(y + 14, gt3r::platform::is_mobile() ? kMobileWhere : "You can also drop the file on this window.", colors::kDim);
        ui.text_center(y + 34, "Nothing is copied from the disc: the game's files are read from the image as they are needed.", colors::kDim);
        y += 60;
        for (const std::string& line : Ui::wrap(app.last_error(), 400)) ui.text_center(y, line, colors::kBad), y += 12; // (as wide as the rows)
        rows_.render(ui, {120, y + 10, 400, 17 * 10});
        ui.footer(kCanQuit ? "{Cross}: choose  {Triangle}: quit" : "{Cross}: choose");
    }

private:
    void build(App& app) {
        App* a = &app;
        rows_.rows.clear();
        rows_.action("Choose a disc image", "", "", [a] { a->show_open_dialog(); });
        for (const std::string& p : discs_) {
            const auto record = app.disc_record(p);
            rows_.action(gt3r::platform::disc_file_name(p), record ? record->description : std::string(), "", [a, p] { a->request_disc(p); });
        }
    }
    PageRows rows_;
    std::vector<std::string> discs_;
    bool known_ = false;
};

} // namespace

std::unique_ptr<Screen> make_setup_screen() { return std::make_unique<SetupScreen>(); }

} // namespace gt4r
