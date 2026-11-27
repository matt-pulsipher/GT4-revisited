#pragma once

// The Car database page's facts (app/screen_database.cpp): every car of the disc with its figures as the game's own pages
// write them, its engine's curves, the parts the tune shops sell for it and the events of Gran Turismo mode it may enter.
// On a Gran Turismo 4 disc the names, the parts and the events come from Gran Turismo mode's scripts and text run behind the
// menus' natives (menu/save_editor.h, no page shown, nothing saved); the regulations are the scripts' own
// (getViolatedRegulatoin, share/regulation.ad). On Prologue the database and the program alone: it has no tune shops and no
// events. docs/formats/menu.md "The Car database".

#include "gt4r/menu/runtime.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gt4r::menu {

class SaveEditor;
struct Database;

class CarDatabase {
public:
    // `config`: as the menus' (its data folder is not used). Reads the database and the program; on a Gran Turismo 4 disc
    // starts Gran Turismo mode's project on a new game. Throws as Runtime does; a disc without a database gives no cars
    // (error()).
    CarDatabase(FileReader read, RuntimeConfig config);
    ~CarDatabase();

    bool prologue() const { return prologue_; }
    const std::string& error() const { return error_; }

    struct Car {
        std::string label, name; // "_117_coupe_68", "Isuzu 117 Coupe '68" in the menus' language
        std::string maker;       // the tuner's name as the dealers write it ("Isuzu")
        std::string country;     // the country's name
        int year = 0;            // model year (0: none)
        s64 price = 0;           // the dealers' price in credits (GENERIC_CAR +0x08)
        bool for_sale = true;    // GENERIC_CAR +0x12 bit 0 clear
        int category = 0;        // +0x11 modulo 100: 0 normal, 1 racing, 2 tuning, 3 concept
        int power = 0;           // the catalog's power in the page's unit (getCatPs), 0: none
        int weight = 0;          // kg (getCatMs), 0: none
        std::string drivetrain;  // "FR", "FF", "4WD", "MR", "RR", "--"
    };
    const std::vector<Car>& cars() const { return cars_; }
    const Car* car(const std::string& label) const;

    // A label and its value as a page shows them.
    struct Row {
        std::string label, value;
        bool heading = false;
        bool bad = false; // a regulation the car breaks
    };
    // The dealer's spec sheet (Type, Displacement, Power, Torque, Dimensions, Weight, Drivetrain, Price) in the game's
    // units for the disc's region, then what the database adds (aspiration, wheelbase, gears, tires, ...).
    std::vector<Row> specs(const std::string& label);

    // The engine's figures as the settings pages chart them (sim::Showroom::charts on the car as sold): the curve's engine
    // speeds with the power and torque there, in the page's units.
    struct Curve {
        std::vector<int> rpm;
        std::vector<float> power, torque;  // in power_unit / torque_unit
        float peak_power = 0, peak_torque = 0;
        int power_rpm = 0, torque_rpm = 0; // the catalog's speeds of the peaks (0: none)
        int rev_limit = 0;
        std::string power_unit, torque_unit;
    };
    std::optional<Curve> curve(const std::string& label);

    // The parts the tune shops sell for the car, by the tune shop's groups (Exhaust, Brakes, Engine, Drivetrain, Turbo,
    // Suspension, Tires, Other): each part kind with its stages and prices (getPartsCondition's price).
    struct Part {
        std::string name;
        s64 price = 0; // credits; 0: none shown
        bool stock = false; // what the car comes with
    };
    struct PartKind {
        std::string group, name;
        std::vector<Part> parts;
    };
    std::vector<PartKind> upgrades(const std::string& label);
    // Whether the app's font draws every letter of a text (the Japanese discs' texts are in kana).
    static bool drawable(const std::string& s);
    // The tune shops' price unit: "Cr." (the program's money text).
    static std::string credits(s64 v);

    // Gran Turismo mode's events (the event halls' lists, EventData::AllEvents) with whether the car as sold may enter each
    // (the scripts' getViolatedRegulatoin on its first race), and the regulations as the event's page lists them.
    struct Event {
        std::string hall, name, code, level;
        bool allowed = false;
        std::string license; // the license the event asks of the driver ("" : none)
        std::vector<Row> rules;
    };
    std::vector<Event> events(const std::string& label);

    // The units the figures are given in ("HP", "ft.lb", ...): the disc's region's, as a new game has them.
    const std::string& power_unit() const { return power_unit_; }
    const std::string& torque_unit() const { return torque_unit_; }

private:
    bool prologue_ = false;
    std::string error_;
    std::vector<Car> cars_;
    std::string power_unit_, torque_unit_;
    float power_factor_ = 1, torque_factor_ = 1;
    std::unique_ptr<SaveEditor> editor_;     // Gran Turismo 4: the mode's scripts and text
    std::unique_ptr<Database> own_;          // Prologue: the database and the program alone
    Database& db();
    std::string text(const std::string& page, const std::string& group, const std::string& key);
    static std::string country_name(const std::string& code); // "JP" -> "Japan"
    std::map<std::string, std::string> names_; // car label -> its name to show
    void read_cars();
};

} // namespace gt4r::menu
