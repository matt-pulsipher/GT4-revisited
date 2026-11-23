// The first screen without a disc: asks for the player's own disc image.

#include "gt4r/app/app.h"

#include "gt4r_version.h"

namespace gt4r {

namespace {

class SetupScreen : public Screen {
public:
    void update(App& app, double) override {
        const MenuInput& m = app.menu();
        if (m.pressed(PadButton::Cross)) app.show_open_dialog();
        if (m.pressed(PadButton::Triangle)) app.quit();
    }

    void render(App& app, Ui& ui) override {
        ui.header("GT4 REVISITED", GT4R_VERSION);
        float y = 70;
        ui.text_center(y, "Choose your Gran Turismo 4 disc image (.iso or .chd).", colors::kText);
        ui.text_center(y + 14, "You can also drop the file on this window.", colors::kDim);
        ui.text_center(y + 34, "Nothing is copied from the disc: the game's files are read from the image as they are needed.", colors::kDim);
        if (!app.last_error().empty()) {
            float ey = y + 70;
            for (const std::string& line : Ui::wrap(app.last_error(), Ui::kWidth - 80)) ui.text_center(ey, line, colors::kBad), ey += 12;
        }
        ui.footer("{Cross}: choose a disc image  {Triangle}: quit");
    }
};

} // namespace

std::unique_ptr<Screen> make_setup_screen() { return std::make_unique<SetupScreen>(); }

} // namespace gt4r
