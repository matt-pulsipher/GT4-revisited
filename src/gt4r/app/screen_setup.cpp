// The first screen without a disc: asks for the player's own disc image, and offers the ones opened before.

#include "gt4r/app/app.h"
#include "gt4r/app/page.h"

#include "base/file.h"
#include "gt4r_version.h"

namespace gt4r {

namespace {

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
        if (app.menu().pressed(PadButton::Triangle)) app.quit();
        else rows_.update(app);
        build(app);
    }

    void render(App& app, Ui& ui) override {
        ui.header("GT4 REVISITED", GT4R_VERSION);
        float y = 62;
        ui.text_center(y, "Choose your Gran Turismo 4 disc image (.iso or .chd).", colors::kText);
        ui.text_center(y + 14, "You can also drop the file on this window.", colors::kDim);
        ui.text_center(y + 34, "Nothing is copied from the disc: the game's files are read from the image as they are needed.", colors::kDim);
        y += 60;
        for (const std::string& line : Ui::wrap(app.last_error(), Ui::kWidth - 80)) ui.text_center(y, line, colors::kBad), y += 12;
        rows_.render(ui, {120, y + 10, 400, 17 * 10});
        ui.footer("{Cross}: choose  {Triangle}: quit");
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
