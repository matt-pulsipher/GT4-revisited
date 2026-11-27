#pragma once

// The app's car picker, shared by every page that lets the player choose any car (Quick race, Custom race, the online lobby): the
// disc's cars by maker, or every car by name, then the car's colors. Names, makers and colors are the disc's own (the menus'
// texts and the car database, read by the code the game uses: menu/save_editor.h), on Gran Turismo 4's discs and Prologue's.
//
//   std::shared_ptr<CarCatalog> cars = CarCatalog::of(app);            // once per disc; throws FormatError without one
//   app.push(make_car_picker(app, cars, {"Your car", now}, [](const PickedCar& c) { ... }));
//
// The picker is a screen of its own over the page that opened it: Cross on a car, then on a color, calls `chosen` and closes it;
// Triangle steps back (from the makers it closes without a choice). Pad, keyboard, mouse and touch as the app's other pages.

#include "gt4r/base/types.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gt4r {

class App;
class Screen;

// Whether the app's pages can show the text: Latin letters alone (their font has no Japanese or Chinese). The Japanese disc's
// menu texts are Japanese: its pages then take the database's American names (which every disc has) or names made of labels.
bool latin_text(std::string_view text);

struct PickedCar {
    std::string label; // the car's label in the database ("ka_01")
    int paint = 0;     // its color by index (the car's paints as the menus count them)
};

class CarCatalog {
public:
    struct Paint {
        std::string name;
        std::array<u8, 3> rgb{};
    };
    struct Car {
        std::string label, name, maker; // the maker as the menus name it ("Toyota"); "" when the disc names none
        std::string maker_key;          // its key ("toyota": Arcade's car lists' names)
        int year = 0;
        u32 price = 0;
    };
    // The catalog of the app's disc, read once and kept while that disc is open.
    static std::shared_ptr<CarCatalog> of(App& app);

    const std::vector<Car>& cars() const { return cars_; } // the disc's order
    const Car* find(const std::string& label) const;
    std::string name(const std::string& label) const; // the label itself when the disc has no such car
    const std::vector<Paint>& paints(const std::string& label); // read when first asked; at least one

    struct Impl;
    ~CarCatalog();

private:
    CarCatalog() = default;
    std::vector<Car> cars_;
    std::vector<std::vector<Paint>> paints_;
    std::vector<bool> paints_read_;
    std::unique_ptr<Impl> impl_;
};

struct CarPickerOptions {
    std::string title = "Choose a car"; // the page's
    PickedCar now;                       // shown first (its maker's list, its color)
    // Which cars may be chosen (null: all); the others are not listed.
    std::function<bool(const CarCatalog::Car&)> allowed;
    bool colors = true; // a color page after the car (else paint 0, or now.paint when the car is now's)
    // Called every frame while the picker is shown (the page under it does not run then); false closes it without a choice.
    std::function<bool(App&)> keep_open;
};

std::unique_ptr<Screen> make_car_picker(App& app, std::shared_ptr<CarCatalog> cars, CarPickerOptions options, std::function<void(const PickedCar&)> chosen);

} // namespace gt4r
