// The home screen's Save editor (docs/SAVE_EDITOR.md): a saved game chosen, then its pages (Career, Garage, Licenses, Events,
// Missions, Prizes, Records, Arcade), each a list as the Controls page's. The changes are made on a copy
// (menu/save_editor.h) and stay there until Save writes them (save/save_edit.h: a backup of the files first, the write read
// back). Pad, keyboard and mouse: up and down choose, left and right change, Cross opens or sets, L1 and R1 turn the page,
// Triangle goes back, Start saves.
//
// GT4R_SAVE_EDITOR="open=<path>", for scripted runs: that save opened at once (a card image, a save's folder).

#include "gt4r/app/app.h"
#include "gt4r/app/editor_popups.h"
#include "gt4r/app/page.h"
#include "gt4r/menu/save_editor.h"
#include "gt4r/menu/save_editor_prologue.h"
#include "gt4r/save/card_image.h"
#include "gt4r/save/save_edit.h"
#include "gt4r/save/save_library.h"

#include "formats/tex1.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>

namespace gt4r {

namespace {

using gt3r::HitList;
using gt3r::PointerInput;
using gt3r::UiRect;
using menu::GameState;
using menu::SaveEditor;

constexpr float kRowH = 17;
constexpr int kShown = 16;
constexpr float kListX = 20, kListW = 400, kListY = 46;

std::string with_commas(s64 v) {
    std::string s = std::to_string(v < 0 ? -v : v);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return v < 0 ? "-" + s : s;
}
std::string credits(s64 v) { return with_commas(v) + " Cr."; }
std::string percent(int v) { return std::to_string(v) + " %"; }
std::string times(int n, const char* one, const char* many) { return with_commas(n) + " " + (n == 1 ? one : many); }
std::string ordinal(int place) { // 0 first
    const int n = place + 1;
    return std::to_string(n) + (n % 100 >= 11 && n % 100 <= 13 ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th");
}
// M'SS.mmm as the game writes it.
std::string time_text(u32 ms) {
    if (ms == menu::kNoTime) return "--'--.---";
    char b[32];
    std::snprintf(b, sizeof b, "%u'%02u.%03u", ms / 60'000, ms / 1'000 % 60, ms % 1'000);
    return b;
}
// A day of the calendar (menu/game_records.cpp's day numbers: the proleptic Gregorian ordinal + 1,721,424).
std::string date_text(int game_day) {
    // Days from 1970-01-01 (the ordinal 719,163).
    s64 z = s64(GameState::kFirstDay) + game_day - 1'721'424 - 719'163 + 719'468;
    const s64 era = (z >= 0 ? z : z - 146'096) / 146'097;
    const unsigned doe = unsigned(z - era * 146'097), yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153, day = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;
    char b[32];
    std::snprintf(b, sizeof b, "%04d-%02u-%02u", int(s64(yoe) + era * 400 + (month <= 2)), month, day);
    return b;
}

struct Row {
    enum class Kind { Heading, Info, Choice, Number, Link } kind = Kind::Info;
    std::string label, value, hint;
    bool enabled = true, accent = false, swatch = false;
    u8 rgb[3] = {};
    std::function<void(int)> change{};
    std::function<void()> activate{}, square{};
    std::string square_hint{}; // "{Square}: ..."
    const char* cross_hint = "open"; // what Cross does
    std::string car{}; // a car shown beside the list (its label)
    bool selectable() const { return kind != Kind::Heading && kind != Kind::Info; }
};

struct Page {
    std::string title;
    std::function<void()> build{};
    int sel = -1, top = 0;
    std::string car{}; // the page's car, shown when the row has none
};

// The cars' pictures of the disc (piece/car_pict/<label>.img), the last one drawn kept.
class CarPictures {
public:
    ~CarPictures() {
        if (texture_) SDL_DestroyTexture(texture_);
    }
    // Drawn `w` wide at (x, y); the height drawn (0: the disc has none).
    float draw(App& app, const std::string& label, float x, float y, float w) {
        if (label != label_) load(app, label);
        if (!texture_) return 0;
        const float h = w * 3 / 4; // (a 4:3 picture whatever its texels, as the menus show it)
        const SDL_FRect at{x, y, w, h};
        SDL_RenderTexture(app.renderer(), texture_, nullptr, &at);
        return h;
    }

private:
    std::string label_;
    SDL_Texture* texture_ = nullptr;
    void load(App& app, const std::string& label) {
        label_ = label;
        if (texture_) SDL_DestroyTexture(texture_);
        texture_ = nullptr;
        if (label.empty()) return;
        try {
            const std::optional<Bytes> data = app.vfs()->read("piece/car_pict/" + label + ".img");
            if (!data || !gt3r::Tex1::is_tex1(*data)) return;
            const gt3r::Tex1 set = gt3r::Tex1::parse(*data);
            if (set.textures.empty()) return;
            gt3r::GsMemory gs;
            set.upload(*data, gs);
            const gt3r::Tex1::Image img = set.decode(gs, 0);
            texture_ = SDL_CreateTexture(app.renderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, int(img.width), int(img.height));
            if (!texture_) return;
            SDL_UpdateTexture(texture_, nullptr, img.rgba.data(), int(img.width) * 4);
            SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
        } catch (const std::exception&) {
            // (no picture)
        }
    }
};

class SaveEditorScreen : public Screen {
public:
    SaveEditorScreen(App& app, std::string card) : app_(app), prologue_(app.disc() && app.disc()->info().serial.rfind("SCPS", 0) == 0) {
        product_ = prologue_ ? "BISCPS-15055" : "BASCUS-97328";
        std::string data = app.save_dir();
        while (data.size() > 1 && (data.back() == '/' || data.back() == '\\')) data.pop_back();
        data_dir_ = data;
        if (db::SpecDb* sdb = app.db()) codes_ = save::database_codes(*sdb);
        read_list();
        if (!card.empty()) open_card(card);
        if (const char* env = std::getenv("GT4R_SAVE_EDITOR"))
            if (const std::string e = env; e.rfind("open=", 0) == 0) open_path(e.substr(5));
    }

    // A memory card image or a save's game data file.
    bool takes_dropped_file(const std::string& path) const override {
        std::string name = std::filesystem::path(path).filename().string();
        for (char& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
        auto ends = [&name](const char* e) { return name.size() >= std::strlen(e) && name.compare(name.size() - std::strlen(e), std::string::npos, e) == 0; };
        return ends(".ps2") || ends(".mcd") || ends("gamedata");
    }

    void on_event(App&, const SDL_Event& e) override {
        if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) {
            if (takes_dropped_file(e.drop.data)) dropped_ = e.drop.data;
            return;
        }
        // (A held key repeats in a list to choose from.)
        const bool held = e.type == SDL_EVENT_KEY_DOWN && e.key.repeat && picker_.active && e.key.scancode != SDL_SCANCODE_RETURN && e.key.scancode != SDL_SCANCODE_ESCAPE;
        if (e.type == SDL_EVENT_KEY_DOWN && (!e.key.repeat || held)) {
            // Typed into the field in front; its frames are not also the pad's keys.
            if (pad_.active) pad_.key(e.key), typed_wait_ = 12;
            else if (text_.active) text_.key(e.key), typed_wait_ = 12;
            else if (picker_.active) picker_.key(e.key), typed_wait_ = 12;
        }
    }

    void update(App& app, double) override {
        take_picked();
        if (files_.waiting()) return;
        const MenuInput& m = app.menu();
        // Keys typed into a field are not also the pad's.
        if (typed_wait_ > 0) --typed_wait_;
        pointer(app);
        if (confirm_.active) return confirm_.update(m);
        if (typed_wait_ > 0) return;
        if (pad_.active) return pad_.update(m);
        if (text_.active) return text_.update(m);
        if (picker_.active) return picker_.update(m);
        if (!editing()) return update_list(app, m);
        rebuild();
        if (m.pressed(PadButton::R1)) return turn(1);
        if (m.pressed(PadButton::L1)) return turn(-1);
        if (m.pressed(PadButton::Start)) return save();
        if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) return back();
        Page& p = page();
        if (const int v = m.vertical()) move(p, v);
        if (p.sel < 0 || p.sel >= int(rows_.size())) return;
        Row& r = rows_[size_t(p.sel)];
        if (!r.selectable() || !r.enabled) return;
        if (const int h = m.horizontal()) {
            if (r.change) r.change(h), touch();
        } else if (m.pressed(PadButton::Cross)) activate(r);
        else if (m.pressed(PadButton::Square) && r.square) r.square(), touch();
    }

    void render(App& app, Ui& ui) override {
        (void)app;
        hits_.clear();
        if (!editing()) render_list(ui);
        else render_pages(ui);
        if (picker_.active) {
            picker_.render(ui, hits_);
            if (const std::string& label = picker_.items[size_t(picker_.sel)].picture; !label.empty()) pictures_.draw(app_, label, picker_.picture.x, picker_.picture.y, picker_.picture.w);
        }
        if (pad_.active) pad_.render(ui, hits_);
        if (text_.active) text_.render(ui, hits_);
        if (confirm_.active) confirm_.render(ui, hits_);
    }

private:
    App& app_;
    bool prologue_ = false;
    std::string product_, data_dir_;
    save::Codes codes_;

    enum Hit { kRow = 1, kTab, kBack, kSave, kLess, kMore, kListItem };
    HitList hits_;
    int typed_wait_ = 0;
    CarPictures pictures_;
    ChoicePopup picker_;
    NumberPopup pad_;
    TextPopup text_;
    QuestionPopup confirm_;

    void pointer(App& app) {
        for (const PointerInput::Action& a : app.pointer()) {
            const gt3r::HitArea* h = hits_.at(a.x, a.y);
            using K = PointerInput::Action::Kind;
            const bool popup = picker_.active || pad_.active || text_.active || confirm_.active;
            if (a.kind == K::Wheel) {
                const int steps = int(std::lround(a.wheel)) * 2;
                if (picker_.active) picker_.wheel(steps);
                else if (!popup && editing()) page().top = std::clamp(page().top - steps, 0, std::max(0, int(rows_.size()) - kShown));
                else if (!popup) list_sel_ = std::clamp(list_sel_ - steps / 2, 0, int(list_.size()) + 1);
                continue;
            }
            if (a.kind == K::Back) {
                if (confirm_.active) confirm_.active = false;
                else if (pad_.active) pad_.active = false;
                else if (text_.active) text_.active = false;
                else if (picker_.active) picker_.active = false;
                else if (editing()) back();
                else app.pop();
                continue;
            }
            if (a.kind != K::Hover && a.kind != K::Click) continue;
            const bool click = a.kind == K::Click;
            if (!h) {
                if (click && picker_.active) picker_.active = false; // (beside the list)
                continue;
            }
            switch (h->id) {
            case kPopupItem:
                picker_.sel = h->arg;
                if (click) picker_.choose(h->arg);
                break;
            case kPopupKey:
                (pad_.active ? pad_.focus : text_.focus) = h->arg;
                if (click) pad_.active ? pad_.press(h->arg) : text_.press(h->arg);
                break;
            case kPopupAnswer:
                confirm_.focus = h->arg;
                if (click) confirm_.answer(h->arg);
                break;
            case kTab:
                if (click) tab_ = h->arg, open_tab();
                break;
            case kSave:
                if (click) save();
                break;
            case kListItem:
                list_sel_ = h->arg;
                if (click) choose_listed();
                break;
            case kRow:
            case kLess:
            case kMore:
                if (h->arg < 0 || h->arg >= int(rows_.size()) || !rows_[size_t(h->arg)].selectable()) break;
                page().sel = h->arg;
                if (click) {
                    Row& r = rows_[size_t(h->arg)];
                    if (!r.enabled) break;
                    if (h->id != kRow && r.change) {
                        const std::function<void(int)> f = r.change;
                        f(h->id == kMore ? 1 : -1), touch();
                    } else activate(r);
                }
                break;
            default: break;
            }
            if (click) break; // (what was drawn may have changed)
        }
    }

    // ---- The list of saves ---------------------------------------------------------------------------------------------
    struct Listed {
        std::string label, value, card;
        bool readable = true;
    };
    std::vector<Listed> list_;
    int list_sel_ = 0, list_top_ = 0;
    std::string message_; // what went wrong, or what was done
    bool bad_ = false;

    void read_list() {
        list_.clear();
        const save::SaveLibrary library(data_dir_, product_);
        if (codes_.code || prologue_)
            for (const save::SavedGame& g : library.games(codes_)) {
                Listed l;
                l.card = g.card, l.readable = g.readable;
                l.label = g.readable ? (g.name.empty() ? "(no name)" : g.name) : "(does not read)";
                if (g.current) l.label += ", the game being played";
                if (g.readable && !prologue_) l.value = credits(g.money) + ", " + times(g.cars, "car", "cars");
                if (g.when) {
                    const std::tm tm = *std::localtime(&g.when);
                    char b[40];
                    std::snprintf(b, sizeof b, "%s%04d-%02d-%02d %02d:%02d", l.value.empty() ? "" : ", ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
                    l.value += b;
                }
                list_.push_back(std::move(l));
            }
        list_sel_ = std::clamp(list_sel_, 0, int(list_.size()) + 1);
    }

    void update_list(App& app, const MenuInput& m) {
        const int count = int(list_.size()) + 2;
        if (const int v = m.vertical()) list_sel_ = std::clamp(list_sel_ + v, 0, count - 1);
        if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) return app.pop();
        if (m.pressed(PadButton::Cross)) choose_listed();
    }
    void choose_listed() {
        message_.clear(), bad_ = false;
        if (list_sel_ < int(list_.size())) open_card(list_[size_t(list_sel_)].card);
        else if (list_sel_ == int(list_.size())) {
            static const SDL_DialogFileFilter kFilters[] = {{"Memory card images", "ps2;mcd"}, {"All files", "*"}};
            files_.open_file(app_, kFilters, 2);
        } else files_.open_folder(app_);
    }

    void render_list(Ui& ui) {
        ui.header("SAVE EDITOR", app_.disc() ? app_.disc()->info().description : std::string());
        const int count = int(list_.size()) + 2;
        list_top_ = std::clamp(list_top_, std::max(0, list_sel_ - kShown + 1), list_sel_);
        for (int i = list_top_; i < std::min(count, list_top_ + kShown); ++i) {
            const float y = 34 + float(i - list_top_) * kRowH;
            const bool on = i == list_sel_, other = i >= int(list_.size());
            ui.fill_round(kListX, y - 3, 600, 15, 3, on ? colors::kHighlight : colors::kPanel);
            ui.text(kListX + 10, y, other ? std::string(i == int(list_.size()) ? "Another save: a memory card image (.ps2, .mcd) or a save's game data file" : "Another save: its folder") : Ui::elide(list_[size_t(i)].label, 270), on ? colors::kText : colors::kDim);
            if (!other) ui.text_right(kListX + 592, y, Ui::elide(list_[size_t(i)].value, 310), list_[size_t(i)].readable ? colors::kDim : colors::kBad);
            hits_.add({kListX, y - 3, 600, kRowH}, kListItem, i);
        }
        if (list_.empty()) ui.text(kListX + 10, 34 + kRowH * 2.5f, "There is no saved game here yet.", colors::kDim);
        message(ui);
        ui.footer("{Up/Down}: select  {Cross}: open  {Triangle}: back");
    }

    void message(Ui& ui) {
        if (message_.empty()) return;
        float y = Ui::kHeight - 29 - 12 * float(Ui::wrap(message_, 600).size() - 1);
        for (const std::string& line : Ui::wrap(message_, 600)) ui.text(kListX, y, line, bad_ ? colors::kBad : colors::kGood), y += 12;
    }
    void fail(std::string text) { message_ = std::move(text), bad_ = true; }
    void done(std::string text) { message_ = std::move(text), bad_ = false; }

    // A path from the system's window or a file dropped on ours.
    FileWindow files_;
    std::string dropped_;
    void take_picked() {
        std::string path, error;
        if (files_.take(path, error) && !error.empty()) return fail(error);
        if (path.empty()) path.swap(dropped_);
        if (path.empty()) return;
        dropped_.clear();
        if (editing() && edited_) return fail("Save or leave this save first.");
        close();
        open_path(path);
    }

    // ---- A save open ---------------------------------------------------------------------------------------------------
    std::optional<save::SaveDocument> document_;
    std::unique_ptr<SaveEditor> editor_;
    std::unique_ptr<menu::PrologueSaveEditor> prologue_editor_;
    bool edited_ = false;
    std::string title_;

    bool editing() const { return document_.has_value(); }
    GameState& state() { return editor_->state(); }

    void open_card(const std::string& card) {
        const std::filesystem::path folder = std::filesystem::path(card) / save::game_folder_name(product_);
        open_place({save::EditPlace::Kind::Folder, folder.string()}, card);
    }
    void open_path(const std::string& path) {
        std::string why;
        const std::optional<save::EditPlace> place = save::edit_place(path, product_, &why);
        if (!place) return fail(why);
        open_place(*place, "");
    }
    void open_place(const save::EditPlace& place, const std::string& card) {
        std::string why;
        std::optional<save::SaveDocument> d = save::SaveDocument::open(place, codes_, product_, &why);
        if (!d) return fail(why);
        try {
            menu::RuntimeConfig config;
            gt3r::Vfs* vfs = app_.vfs();
            config.list = [vfs](const std::string& folder) { return vfs->list_dir(folder); };
            if (Disc* disc = app_.disc()) config.program_image = [disc] { return disc->program_image(); };
            if (prologue_) config.region = "jp";
            if (const std::vector<std::string> languages = menu::project_languages(config.list, prologue_ ? "prologue" : "boot"); !languages.empty()) config.language = languages[0];
            auto read = [vfs](const std::string& path) { return vfs->read(path); };
            if (prologue_) {
                prologue_editor_ = std::make_unique<menu::PrologueSaveEditor>(read, config);
                prologue_editor_->start(d->state);
            } else {
                if (!editor_) editor_ = std::make_unique<SaveEditor>(read, config);
                editor_->start(d->state);
            }
        } catch (const std::exception& e) {
            editor_.reset(), prologue_editor_.reset();
            return fail(std::string("The save could not be opened with this disc: ") + e.what());
        }
        document_ = std::move(d);
        title_ = place.kind == save::EditPlace::Kind::Image ? std::filesystem::path(place.path).filename().string() : card.empty() ? place.path : "Saved game";
        edited_ = false, message_.clear();
        tab_ = 0, stack_.clear(), open_tab(), dirty_ = true;
    }
    void close() {
        document_.reset(), stack_.clear(), rows_.clear(), edited_ = false;
        read_list();
    }

    void take_state() { document_->state = prologue_ ? prologue_editor_->state() : editor_->state(); }

    void save() {
        take_state();
        if (!document_->changed()) {
            edited_ = false;
            return done("There is nothing to save: no change was made.");
        }
        const save::SaveDocument::Written w = document_->write((std::filesystem::path(data_dir_) / "backups").string());
        if (!w.ok) return fail(w.message + (w.backup.empty() ? std::string() : " The backup is in " + w.backup));
        edited_ = false;
        done("Saved. The save as it was is kept in " + w.backup);
    }

    void back() {
        message_.clear();
        if (stack_.size() > 1) {
            stack_.pop_back();
            dirty_ = true;
            return;
        }
        take_state();
        if (edited_ && document_->changed()) {
            confirm_.open("This save has changes that are not written.", {{"Save them", [this] {
                                                                     save();
                                                                     if (!bad_) close();
                                                                 }},
                                                                {"Leave without saving", [this] { close(); }},
                                                                {"Stay", [] {}}});
            return;
        }
        close();
    }

    // ---- Pages ---------------------------------------------------------------------------------------------------------
    std::vector<Page> stack_;
    std::vector<Row> rows_;
    bool dirty_ = true;
    int tab_ = 0;

    std::vector<std::string> tabs() const {
        if (prologue_) return {"Career", "Licenses"};
        return {"Career", "Garage", "Licenses", "Events", "Missions", "Prizes", "Records", "Arcade"};
    }
    Page& page() { return stack_.back(); }
    void push(std::string title, std::function<void()> build) { stack_.push_back({std::move(title), std::move(build)}), dirty_ = true, message_.clear(); }
    void open_tab() {
        stack_.clear();
        message_.clear();
        const std::string name = tabs()[size_t(tab_)];
        std::function<void()> build;
        if (prologue_) build = tab_ == 0 ? std::function<void()>([this] { build_prologue(); }) : [this] { build_prologue_licenses(); };
        else {
            const std::function<void()> all[8] = {[this] { build_career(); }, [this] { build_garage(); }, [this] { build_licenses(); }, [this] { build_events(); },
                                                  [this] { build_missions(); }, [this] { build_prizes(); }, [this] { build_records(); }, [this] { build_arcade(); }};
            build = all[tab_];
        }
        stack_.push_back({name, std::move(build)});
        dirty_ = true;
    }
    void turn(int d) {
        const int n = int(tabs().size());
        tab_ = (tab_ + d + n) % n;
        open_tab();
    }
    void touch() { dirty_ = true; }
    // A change made by a row.
    void changed() { edited_ = true, dirty_ = true, message_.clear(); }

    void rebuild() {
        if (!dirty_) return;
        dirty_ = false;
        rows_.clear();
        Page& p = page();
        try {
            p.build();
        } catch (const std::exception& e) {
            fail(e.what());
        }
        if (p.sel >= int(rows_.size())) p.sel = int(rows_.size()) - 1;
        if (p.sel < 0 || !rows_[size_t(p.sel)].selectable()) {
            int at = std::max(p.sel, 0);
            p.sel = -1;
            for (int i = at; i < int(rows_.size()) && p.sel < 0; ++i)
                if (rows_[size_t(i)].selectable()) p.sel = i;
            for (int i = std::min(at, int(rows_.size()) - 1); i >= 0 && p.sel < 0; --i)
                if (rows_[size_t(i)].selectable()) p.sel = i;
        }
        show(p);
    }
    void show(Page& p) {
        const int n = int(rows_.size());
        if (p.sel >= 0) {
            // (a heading over the row stays in the picture)
            const int above = p.sel > 0 && !rows_[size_t(p.sel - 1)].selectable() ? p.sel - 1 : p.sel;
            if (above < p.top) p.top = above;
            if (p.sel >= p.top + kShown) p.top = p.sel - kShown + 1;
        }
        p.top = std::clamp(p.top, 0, std::max(0, n - kShown));
    }
    void move(Page& p, int v) {
        for (int i = p.sel + v; i >= 0 && i < int(rows_.size()); i += v)
            if (rows_[size_t(i)].selectable()) {
                p.sel = i;
                break;
            }
        if (v < 0 && p.sel >= 0) { // (at the first row: what is over it shows)
            bool first = true;
            for (int i = 0; i < p.sel; ++i) first = first && !rows_[size_t(i)].selectable();
            if (first) p.top = 0;
        }
        show(p);
    }
    void activate(Row& r) {
        if (!r.selectable() || !r.enabled) return;
        if (r.activate) {
            const std::function<void()> f = r.activate; // (the rows may be made again by it)
            f();
        } else if (r.change) {
            const std::function<void(int)> f = r.change;
            f(1);
        }
        touch();
    }

    // Row makers.
    void heading(std::string label) { rows_.push_back({Row::Kind::Heading, std::move(label), {}, {}}); }
    void info(std::string label, std::string value, std::string hint = {}) { rows_.push_back({Row::Kind::Info, std::move(label), std::move(value), std::move(hint)}); }
    Row& link(std::string label, std::string value, std::string hint, std::function<void()> f) {
        Row r{Row::Kind::Link, std::move(label), std::move(value), std::move(hint)};
        r.activate = std::move(f);
        rows_.push_back(std::move(r));
        return rows_.back();
    }
    Row& choice(std::string label, const std::vector<std::string>& options, int now, std::function<void(int)> set, std::string hint = {}) {
        Row r{Row::Kind::Choice, std::move(label), now >= 0 && now < int(options.size()) ? options[size_t(now)] : std::string("-"), std::move(hint)};
        const int n = int(options.size());
        r.change = [this, now, n, set = std::move(set)](int d) {
            const int to = now < 0 ? (d > 0 ? 0 : n - 1) : (now + d + n) % n;
            if (to != now) set(to), changed();
        };
        rows_.push_back(std::move(r));
        return rows_.back();
    }
    Row& number(std::string label, s64 now, s64 lo, s64 hi, s64 step, std::function<void(s64)> set, std::string hint = {}, std::function<std::string(s64)> text = {}) {
        Row r{Row::Kind::Number, label, text ? text(now) : with_commas(now), std::move(hint)};
        auto put = [this, now, lo, hi, set](s64 v) {
            v = std::clamp(v, lo, hi);
            if (v != now) set(v), changed();
        };
        r.change = [put, now, step](int d) { put(d > 0 ? (now / step + 1) * step : (now % step ? now / step : now / step - 1) * step); };
        r.activate = [this, label, now, lo, hi, put] { pad_.open(label, now, lo, hi, false, put); };
        rows_.push_back(std::move(r));
        return rows_.back();
    }

    void render_pages(Ui& ui) {
        rebuild();
        ui.header("SAVE EDITOR", title_ + (edited_ ? "  (changed, not saved)" : ""));
        float x = kListX;
        const std::vector<std::string> names = tabs();
        for (size_t i = 0; i < names.size(); ++i) {
            const float w = ui.tab(x, 23, 15, names[i], int(i) == tab_, false);
            hits_.add({x, 23, w, 15}, kTab, int(i));
            x += w + 4;
        }
        const UiRect save_button{556, 23, 64, 15};
        ui.button(save_button, "Save", false, false, edited_);
        hits_.add(save_button, kSave);
        Page& p = page();
        const int n = int(rows_.size());
        for (int i = p.top; i < std::min(n, p.top + kShown); ++i) {
            const Row& r = rows_[size_t(i)];
            const float y = kListY + float(i - p.top) * kRowH;
            const bool on = i == p.sel;
            if (r.kind == Row::Kind::Heading) {
                ui.text(kListX + 2, y, Ui::elide(r.label, kListW), colors::kAccent);
                continue;
            }
            if (r.selectable()) {
                ui.fill_round(kListX, y - 3, kListW, 15, 3, on ? colors::kHighlight : colors::kPanel);
                hits_.add({kListX, y - 3, kListW, kRowH}, kRow, i);
            }
            const Color c = !r.enabled ? Color{90, 90, 100} : r.selectable() ? (on ? colors::kText : colors::kDim) : colors::kDim;
            float right = kListX + kListW - 8;
            if (on && r.enabled && r.change) {
                ui.text_right(right, y, ">", colors::kAccent);
                hits_.add({right - 10, y - 3, 18, kRowH}, kMore, i);
                right -= 12;
            } else if (r.kind == Row::Kind::Link && r.enabled) {
                ui.text_right(right, y, ">", on ? colors::kAccent : colors::kDim);
                right -= 12;
            }
            const std::string value = Ui::elide(r.value, 230);
            const float vw = value.empty() ? 0 : Ui::text_width(value);
            ui.text_right(right, y, value, r.accent ? colors::kAccent : c);
            float left = right - vw;
            if (r.swatch) {
                ui.fill_round(left - 18, y - 1, 14, 10, 2, Color{r.rgb[0], r.rgb[1], r.rgb[2]});
                ui.frame_round(left - 18, y - 1, 14, 10, 2, 1, colors::kOutline);
                left -= 18;
            }
            if (on && r.enabled && r.change) {
                ui.text_right(left - 6, y, "<", colors::kAccent);
                hits_.add({left - 18, y - 3, 18, kRowH}, kLess, i);
                left -= 16;
            }
            ui.text(kListX + 10, y, Ui::elide(r.label, std::max(60.0f, left - kListX - 18)), c);
        }
        if (n > kShown) ui.scrollbar({kListX, kListY - 3, kListW + 8, kShown * kRowH}, float(n) * kRowH, float(p.top) * kRowH);
        // Beside the list: what the row is.
        const float px = 440, pw = 180;
        float y = kListY;
        if (stack_.size() > 1) {
            for (const std::string& line : Ui::wrap(page().title, pw)) ui.text(px, y, line, colors::kAccent), y += 12;
            y += 4;
        }
        std::string foot = "{Up/Down}: select";
        if (p.sel >= 0 && p.sel < n) {
            const Row& r = rows_[size_t(p.sel)];
            for (const std::string& line : Ui::wrap(r.hint, pw)) ui.text(px, y, line, colors::kDim), y += 12;
            if (r.enabled) {
                if (r.change) foot += "  {Left/Right}: change";
                if (r.activate) foot += std::string("  {Cross}: ") + (r.kind == Row::Kind::Number ? "type it" : r.cross_hint);
                if (r.square && !r.square_hint.empty()) foot += "  " + r.square_hint;
            }
        }
        if (!picker_.active) {
            const std::string& car = p.sel >= 0 && p.sel < n && !rows_[size_t(p.sel)].car.empty() ? rows_[size_t(p.sel)].car : p.car;
            if (!car.empty()) pictures_.draw(app_, car, px, y + 4, 140);
        }
        if (!prologue_) {
            y = kListY + 12 * kRowH;
            ui.text(px, y, Ui::elide(state().username, pw), colors::kText), y += 14;
            ui.text(px, y, credits(state().money), colors::kDim), y += 12;
            ui.text(px, y, "Day " + with_commas(editor_->days() + 1), colors::kDim), y += 12;
            ui.text(px, y, times(state().garage_count(), "car", "cars"), colors::kDim);
        }
        message(ui);
        ui.footer(foot + "  {L1} {R1}: page  {Start}: save  {Triangle}: back");
    }

    // ---- Career --------------------------------------------------------------------------------------------------------
    void build_career() {
        SaveEditor& e = *editor_;
        GameState& s = state();
        link("Name", s.username, "Up to ten letters, as on the game's own name page.", [this] { text_.open("Name", state().username, SaveEditor::kNameLetters, [this](const std::string& v) {
            if (v != state().username) editor_->set_name(v), changed();
        }); });
        number("Credits", s.money, 0, std::max(SaveEditor::kMaxMoney, s.money), 100'000, [this](s64 v) { editor_->set_money(v); }, "Left and right change it by 100,000.", credits);
        number("Day", e.days() + 1, 1, SaveEditor::kMaxDays + 1, 1, [this](s64 v) { editor_->set_days(int(v) - 1); },
               "The used car lots and some presents go by it. Going back takes the days after it out of the diary.",
               [](s64 v) { return "Day " + with_commas(v) + ", " + date_text(int(v) - 1); });
        const int done = e.completion();
        info("Complete", std::to_string(done / 10) + "." + std::to_string(done % 10) + " %");
        info("A-spec points", with_commas(e.aspec_points()) + " pts");
        heading("B-spec driver");
        const SaveEditor::BSpec b = e.bspec();
        info("B-spec Skill", with_commas(b.skill));
        info("Machine / Course / Battle Skill", std::to_string(b.machine) + " / " + std::to_string(b.course) + " / " + std::to_string(b.battle));
        int all = 0;
        for (int k = 0; k < SaveEditor::kCarClasses; ++k) all += e.bspec_class_level(k);
        all /= SaveEditor::kCarClasses;
        auto level = [](s64 v) { return percent(int(v)); };
        number("Experience with every car", all, 0, 100, 5, [this](s64 v) { for (int k = 0; k < SaveEditor::kCarClasses; ++k) editor_->set_bspec_class_level(k, int(v)); },
               "How much the B-spec driver has learned, for all eight classes of cars at once.", level);
        link("Experience by class of car", "", "Each class of cars by itself.", [this] {
            push("Experience by class of car", [this] {
                for (int k = 0; k < SaveEditor::kCarClasses; ++k)
                    number("Class " + std::to_string(k + 1) + (k == 0 ? " (the slowest cars)" : k == 7 ? " (the fastest cars)" : ""), editor_->bspec_class_level(k), 0, 100, 5,
                           [this, k](s64 v) { editor_->set_bspec_class_level(k, int(v)); }, "", [](s64 v) { return percent(int(v)); });
            });
        });
        number("Knowledge of the courses", e.bspec_course_level(), 0, 100, 5, [this](s64 v) { editor_->set_bspec_course_level(int(v)); }, "Of all the courses the B-spec driver can learn.", level);
        heading("Totals");
        number("Total prize credits", s.total_prize_money, 0, std::max(SaveEditor::kMaxMoney, s.total_prize_money), 100'000, [this](s64 v) { state().total_prize_money = v; }, "", credits);
        number("Total prize cars", s.total_prize_cars, 0, SaveEditor::kMaxCount, 1, [this](s64 v) { state().total_prize_cars = u32(v); }, "The Status page's count: no car comes with it.");
        for (int p = 1; p < 7; ++p) number("Races finished " + ordinal(p - 1), s.rank_counts[size_t(p)], 0, SaveEditor::kMaxCount, 1, [this, p](s64 v) { editor_->set_finishes(p, u32(v)); }, p == 1 ? "The wins of the Status page." : "");
        number("Races not finished", s.rank_counts[0], 0, SaveEditor::kMaxCount, 1, [this](s64 v) { editor_->set_finishes(0, u32(v)); });
        const bool miles = std::string(e.distance_unit()) == "miles";
        const s64 unit = miles ? 1'609 : 1'000;
        for (bool aspec : {true, false}) {
            s64& m = aspec ? s.aspec_distance : s.bspec_distance;
            number(aspec ? "A-spec mileage" : "B-spec mileage", m / unit, 0, 9'999'999, 100, [this, aspec, unit](s64 v) { (aspec ? state().aspec_distance : state().bspec_distance) = v * unit; }, "",
                   [miles](s64 v) { return with_commas(v) + (miles ? " miles" : " km"); });
        }
        heading("Also kept in the save");
        choice("Ending movie", {"Not seen yet", "Can be watched"}, e.ending_seen(), [this](int i) { editor_->set_ending_seen(i == 1); });
        choice("Credits from Gran Turismo 3", {"Can still be brought over", "Already brought over"}, s.withdrawn != 0, [this](int i) { state().withdrawn = i; });
        choice("Licenses from Prologue", {"Can still be brought over", "Already brought over"}, s.withdrawn_gt4p != 0, [this](int i) { state().withdrawn_gt4p = i; });
        if (const std::string c = e.championship_under_way(); !c.empty())
            link("Championship under way", c, "Give it up: it starts again from its first race.", [this, c] {
                confirm_.open("Give up " + c + "?", {{"Give it up", [this] { editor_->abandon_championship(), changed(); }}, {"Keep it", [] {}}});
            });
        if (const int sold = e.used_sold())
            link("Used cars sold this week", times(sold, "car", "cars"), "Put them back on sale.", [this] { editor_->restock_used(), changed(); }).cross_hint = "choose";
    }

    // ---- Garage --------------------------------------------------------------------------------------------------------
    std::string car_figures(int slot) {
        const SaveEditor::Figures f = editor_->figures(slot);
        std::string out;
        if (f.hp) out = std::to_string(f.hp) + " " + f.power_unit;
        if (f.kg) out += (out.empty() ? "" : ", ") + with_commas(f.kg) + " kg";
        return out;
    }
    void build_garage() {
        GameState& s = state();
        const int count = s.garage_count();
        link("Add a car", times(count, "car", "cars") + " of 1,000", "Any car of the game, new, in the paint you choose. It can also be driven in Arcade mode.", [this] { pick_maker(); }).enabled = count < int(GameState::kGarageSize);
        if (s.riding_car >= 0) heading("Your car now");
        auto car_row = [&](int i) {
            const menu::GarageEntry& g = s.garage[size_t(i)];
            Row& r = link(editor_->car_name(g.car), car_figures(i), "", [this, i] { open_car(i); });
            const std::vector<SaveEditor::Paint> paints = editor_->paints(g.car);
            const int p = editor_->car_paint(i);
            if (p >= 0 && p < int(paints.size())) {
                r.swatch = true, std::copy_n(paints[size_t(p)].rgb, 3, r.rgb);
                r.hint = paints[size_t(p)].name;
            }
            r.hint += (r.hint.empty() ? "" : ". ") + with_commas(editor_->odometer(i)) + " " + editor_->distance_unit() + ". Got on day " + with_commas(editor_->got_on_day(i) + 1) + ".";
            r.car = g.car;
            r.square = [this, i] { remove_car(i); };
            r.square_hint = "{Square}: remove";
        };
        if (s.riding_car >= 0 && size_t(s.riding_car) < s.garage.size() && s.garage[size_t(s.riding_car)].used) car_row(s.riding_car);
        heading("The garage");
        for (size_t i = 0; i < s.garage.size(); ++i)
            if (s.garage[i].used && int(i) != s.riding_car) car_row(int(i));
    }
    void remove_car(int slot) {
        const std::string name = editor_->car_name(state().garage[size_t(slot)].car);
        confirm_.open("Remove the " + name + " from the garage?" + (slot == state().riding_car ? " It is your car now." : ""), {{"Remove it", [this, slot] {
                                                                                                                                      editor_->remove_car(slot);
                                                                                                                                      if (stack_.size() > 1) stack_.pop_back();
                                                                                                                                      changed();
                                                                                                                                  }},
                                                                                                                                 {"Keep it", [] {}}});
    }
    void pick_maker() {
        std::map<std::string, int> makers;
        for (const SaveEditor::CarKind& k : editor_->cars()) ++makers[k.maker];
        std::vector<ChoicePopup::Item> items{{"Every maker", with_commas(s64(editor_->cars().size()))}};
        std::vector<std::string> names{""};
        for (const auto& [m, n] : makers) items.push_back({m, with_commas(n)}), names.push_back(m);
        picker_.open("Add a car: whose?", std::move(items), 0, [this, names](int i) { pick_car(names[size_t(i)]); });
    }
    void pick_car(const std::string& maker) {
        std::vector<ChoicePopup::Item> items;
        std::vector<std::string> labels;
        std::vector<const SaveEditor::CarKind*> cars;
        for (const SaveEditor::CarKind& k : editor_->cars())
            if (maker.empty() || k.maker == maker) cars.push_back(&k);
        std::sort(cars.begin(), cars.end(), [](const auto* a, const auto* b) { return a->maker != b->maker ? a->maker < b->maker : a->year != b->year ? a->year < b->year : a->name < b->name; });
        for (const SaveEditor::CarKind* k : cars) {
            ChoicePopup::Item it;
            it.label = k->name, it.note = k->price ? credits(k->price) : "", it.picture = k->label;
            items.push_back(std::move(it)), labels.push_back(k->label);
        }
        picker_.open("Add a car: which?", std::move(items), 0, [this, labels](int i) { pick_paint(labels[size_t(i)], 0, "Add it in which paint?", [this, label = labels[size_t(i)]](int paint) { add_car(label, paint); }); });
    }
    void pick_paint(const std::string& label, int now, const std::string& title, std::function<void(int)> f) {
        std::vector<ChoicePopup::Item> items;
        for (const SaveEditor::Paint& p : editor_->paints(label)) {
            ChoicePopup::Item it;
            it.label = p.name.empty() ? "Paint" : p.name, it.swatch = true, std::copy_n(p.rgb, 3, it.rgb), it.picture = label;
            items.push_back(std::move(it));
        }
        if (items.size() < 2) return f(0);
        if (now >= 0 && now < int(items.size())) items[size_t(now)].mark = true;
        picker_.open(title, std::move(items), now, std::move(f));
    }
    void add_car(const std::string& label, int paint) {
        const int slot = editor_->add_car(label, paint);
        if (slot < 0) return fail("The garage is full.");
        changed();
        open_car(slot);
    }
    void open_car(int slot) {
        push(editor_->car_name(state().garage[size_t(slot)].car), [this, slot] { build_car(slot); });
        page().car = state().garage[size_t(slot)].car;
    }
    void build_car(int slot) {
        SaveEditor& e = *editor_;
        GameState& s = state();
        if (size_t(slot) >= s.garage.size() || !s.garage[size_t(slot)].used) return;
        const menu::GarageEntry& g = s.garage[size_t(slot)];
        info(e.car_name(g.car), car_figures(slot));
        if (slot == s.riding_car) info("Your car now", "This one");
        else link("Your car now", "Drive this one", "It becomes the car you are driving.", [this, slot] { editor_->set_active(slot), changed(); }).cross_hint = "choose";
        const std::vector<SaveEditor::Paint> paints = e.paints(g.car);
        const int paint = e.car_paint(slot);
        if (!paints.empty()) {
            Row r{Row::Kind::Choice, "Paint", paint >= 0 && paint < int(paints.size()) ? paints[size_t(paint)].name : "-", ""};
            if (paint >= 0 && paint < int(paints.size())) r.swatch = true, std::copy_n(paints[size_t(paint)].rgb, 3, r.rgb);
            r.enabled = paints.size() > 1;
            r.change = [this, slot, paint](int d) { editor_->set_car_paint(slot, paint + d), changed(); };
            r.activate = [this, slot, paint, label = g.car] { pick_paint(label, paint, "Paint", [this, slot](int p) { editor_->set_car_paint(slot, p), changed(); }); };
            rows_.push_back(std::move(r));
        }
        number("Odometer", e.odometer(slot), 0, e.max_odometer(), 1'000, [this, slot](s64 v) { editor_->set_odometer(slot, u32(v)); }, "", [this](s64 v) { return with_commas(v) + " " + editor_->distance_unit(); });
        number("Got on day", e.got_on_day(slot) + 1, 1, e.days() + 1, 1, [this, slot](s64 v) { editor_->set_got_on_day(slot, int(v) - 1); }, "", [](s64 v) { return with_commas(v); });
        heading("Condition");
        if (e.no_wear(slot)) info("This car does not wear", "");
        else {
            choice("Engine", {"New, not run in", "Run in", "Worn out"}, int(e.engine(slot)), [this, slot](int i) { editor_->set_engine(slot, SaveEditor::Engine(i)); });
            auto worn = [](const char* none, const char* some) { return [none, some](s64 v) { return v == 0 ? std::string(none) : percent(int(v)) + " " + some; }; };
            number("Oil", e.oil_used(slot), 0, 100, 5, [this, slot](s64 v) { editor_->set_oil_used(slot, int(v)); }, "", worn("Fresh", "used"));
            number("Body stiffness", e.body_worn(slot), 0, 100, 5, [this, slot](s64 v) { editor_->set_body_worn(slot, int(v)); }, "", worn("As new", "worn"));
        }
        number("Dirt", e.dirt(slot), 0, 100, 10, [this, slot](s64 v) { editor_->set_dirt(slot, int(v)); }, "", [](s64 v) { return v == 0 ? std::string("Clean") : percent(int(v)) + " dirty"; });
        heading("Parts and settings");
        choice("Settings sheet", {"Set A", "Set B", "Set C"}, e.sheet(slot), [this, slot](int i) { editor_->set_sheet(slot, i); }, "The parts fitted and the settings below are this sheet's.");
        link("Parts", "", "What is fitted and what the car has in stock.", [this, slot] { push("Parts", [this, slot] { build_parts(slot); }), page().car = state().garage[size_t(slot)].car; });
        link("Settings", "", "What the fitted parts let you set.", [this, slot] { push("Settings", [this, slot] { build_settings(slot); }), page().car = state().garage[size_t(slot)].car; });
        static const char* const kTires[4] = {"Wheels for road tires", "Wheels for racing tires", "Wheels for dirt tires", "Wheels for snow tires"};
        if (e.wheels_allowed(slot))
            for (int c = 0; c < 4; ++c) link(kTires[c], extra_name(e.wheels(), e.wheel(slot, c), "The car's own"), "", [this, slot, c] { pick_extra(kTires[c], editor_->wheels(), editor_->wheel(slot, c), "The car's own", [this, slot, c](const std::string& l) { editor_->set_wheel(slot, c, l); }); });
        if (e.wing_allowed(slot)) link("Wing", extra_name(e.wings(), e.wing(slot), "None, or the car's own"), "", [this, slot] { pick_extra("Wing", editor_->wings(), editor_->wing(slot), "None, or the car's own", [this, slot](const std::string& l) { editor_->set_wing(slot, l); }); });
        heading("");
        link("Remove this car", "", "", [this, slot] { remove_car(slot); }).cross_hint = "choose";
    }
    static std::string extra_name(const std::vector<SaveEditor::Extra>& all, const std::string& label, const char* none) {
        if (label.empty()) return none;
        for (const SaveEditor::Extra& x : all)
            if (x.label == label) return x.name;
        return label;
    }
    void pick_extra(const std::string& title, const std::vector<SaveEditor::Extra>& all, const std::string& now, const char* none, std::function<void(const std::string&)> f) {
        std::vector<ChoicePopup::Item> items{{none, ""}};
        std::vector<std::string> labels{""};
        int at = 0;
        for (const SaveEditor::Extra& x : all) {
            if (x.label == now) at = int(items.size());
            items.push_back({x.name, ""}), labels.push_back(x.label);
        }
        items[size_t(at)].mark = true;
        picker_.open(title, std::move(items), at, [this, labels, f = std::move(f)](int i) { f(labels[size_t(i)]), changed(); });
    }
    void build_parts(int slot) {
        link("Have every part", "", "Every part this car can take is in stock. Work on the engine and the body is not done by it.", [this, slot] { editor_->own_all_parts(slot), changed(); }).cross_hint = "choose";
        bool work = false;
        for (const SaveEditor::PartKind& k : editor_->parts(slot)) {
            if (k.permanent && !work) heading("Work that is not undone in the game"), work = true;
            std::vector<std::string> names;
            for (const SaveEditor::PartOption& o : k.options) names.push_back(o.name);
            const int type = k.type;
            std::vector<int> categories;
            for (const SaveEditor::PartOption& o : k.options) categories.push_back(o.category);
            int owned = 0;
            for (const SaveEditor::PartOption& o : k.options) owned += o.owned;
            Row& r = choice(k.name, names, k.fitted, [this, slot, type, categories](int i) { editor_->fit_part(slot, type, categories[size_t(i)]); },
                            k.permanent ? "" : "A part that is fitted is in stock from then on. In stock: " + std::to_string(owned) + " of " + std::to_string(k.options.size()) + ".");
            r.activate = [this, slot, type, kind = k] {
                std::vector<ChoicePopup::Item> items;
                for (const SaveEditor::PartOption& o : kind.options) {
                    ChoicePopup::Item it;
                    it.label = o.name, it.mark = o.fitted, it.note = o.fitted ? (kind.permanent ? "now" : "fitted") : kind.permanent ? "" : o.owned ? "in stock" : "not in stock";
                    items.push_back(std::move(it));
                }
                picker_.open(kind.name, std::move(items), kind.fitted, [this, slot, type, kind](int i) { editor_->fit_part(slot, type, kind.options[size_t(i)].category), changed(); });
            };
            if (!k.permanent) {
                r.square = [this, slot, type, options = k.options] {
                    // In stock or not, all of this kind's parts but the fitted one.
                    bool missing = false;
                    for (const SaveEditor::PartOption& o : options) missing = missing || !o.owned;
                    for (const SaveEditor::PartOption& o : options)
                        if (o.category > 0 || missing) editor_->own_part(slot, type, o.category, missing);
                    changed();
                };
                r.square_hint = owned < int(k.options.size()) ? "{Square}: have them all" : "{Square}: have only the fitted one";
            }
        }
    }
    void build_settings(int slot) {
        const std::vector<SaveEditor::Setting> all = editor_->settings(slot);
        if (all.empty()) info("The fitted parts have nothing to set", "");
        std::string group;
        for (const SaveEditor::Setting& s : all) {
            if (s.group != group) heading(s.group), group = s.group;
            Row r{Row::Kind::Number, s.name, editor_->setting_text(s), ""};
            SaveEditor::Setting lo = s, hi = s;
            lo.value = s.lo, hi.value = s.hi;
            r.hint = editor_->setting_text(lo) + " to " + editor_->setting_text(hi);
            auto put = [this, slot, s](s64 v) {
                v = std::clamp<s64>(v, s.lo, s.hi);
                if (v != s.value) editor_->set_setting(slot, s, s32(v)), changed();
            };
            const int step = s.hi - s.lo > 400 ? 10 : 1;
            r.change = [put, s, step](int d) { put(s64(s.value) + d * step); };
            // (Typed as the page shows it when that is a whole number from 0 on.)
            if (s.decimals == 0 && s.lo + s.offset >= 0)
                r.activate = [this, put, s] { pad_.open(s.name, s.value + s.offset, s.lo + s.offset, s.hi + s.offset, false, [put, s](s64 v) { put(v - s.offset); }); };
            rows_.push_back(std::move(r));
        }
    }

    // ---- Licenses ------------------------------------------------------------------------------------------------------
    static std::string medal_name(int m) { return m == SaveEditor::kGold ? "Gold" : m == SaveEditor::kSilver ? "Silver" : m == SaveEditor::kBronze ? "Bronze" : "No medal"; }
    void build_licenses() {
        link("Every license, every test", "", "One medal for all tests.", [this] {
            picker_.open("Every test of every license", {{"Gold", ""}, {"Silver", ""}, {"Bronze", ""}, {"No medal", ""}}, 0, [this](int i) {
                for (const SaveEditor::License& l : editor_->licenses()) set_license(l, SaveEditor::Medal(3 - i));
                changed();
            });
        });
        for (size_t k = 0; k < editor_->licenses().size(); ++k) {
            const SaveEditor::License& l = editor_->licenses()[k];
            int n[4] = {};
            for (const SaveEditor::LicenseTest& t : l.tests) ++n[editor_->medal(t)];
            std::string value = std::to_string(n[3]) + " gold, " + std::to_string(n[2]) + " silver, " + std::to_string(n[1]) + " bronze";
            Row& r = link(l.name, value, l.category == "l0c" ? "" : editor_->license_held(l) ? "The license is held." : "The license is not held yet.", [this, k] { push(editor_->licenses()[k].name, [this, k] { build_license(k); }); });
            r.accent = l.category != "l0c" && editor_->license_held(l);
        }
    }
    void set_license(const SaveEditor::License& l, SaveEditor::Medal m) {
        for (const SaveEditor::LicenseTest& t : l.tests) {
            // (The coffee breaks have no bronze: the least they give.)
            SaveEditor::Medal to = m;
            while (to != SaveEditor::kNone && !t.limit[to] && to < SaveEditor::kGold) to = SaveEditor::Medal(to + 1);
            editor_->set_medal(t, to);
        }
    }
    void build_license(size_t k) {
        const SaveEditor::License& l = editor_->licenses()[k];
        info(l.name, l.category == "l0c" ? "" : editor_->license_held(l) ? "Held" : "Not held");
        link("Every test", "", "One medal for all tests of this license.", [this, k] {
            picker_.open("Every test: " + editor_->licenses()[k].name, {{"Gold", ""}, {"Silver", ""}, {"Bronze", ""}, {"No medal", ""}}, 0,
                         [this, k](int i) { set_license(editor_->licenses()[k], SaveEditor::Medal(3 - i)), changed(); });
        });
        for (size_t i = 0; i < l.tests.size(); ++i) {
            const SaveEditor::LicenseTest& t = l.tests[i];
            std::vector<std::string> names{"No medal"};
            std::vector<int> medals{0};
            for (int m = 1; m <= 3; ++m)
                if (t.limit[m] || m == SaveEditor::kGold) names.push_back(medal_name(m)), medals.push_back(m);
            const int now = int(std::find(medals.begin(), medals.end(), int(editor_->medal(t))) - medals.begin());
            const u32 best = editor_->best_time(t);
            for (std::string& n : names)
                if (best != menu::kNoTime) n += ", " + time_text(best);
            std::string hint;
            for (int m = 3; m >= 1; --m)
                if (t.limit[m]) hint += (hint.empty() ? "" : ", ") + medal_name(m) + " " + time_text(t.limit[m]);
            hint += ".";
            Row& r = choice(std::to_string(i + 1) + ". " + t.name, names, now < int(names.size()) ? now : 0, [this, k, i, medals](int c) { editor_->set_medal(editor_->licenses()[k].tests[i], SaveEditor::Medal(medals[size_t(c)])); }, hint);
            r.accent = editor_->medal(t) == SaveEditor::kGold;
            if (const u32 most = t.limit[SaveEditor::kBronze]) {
                r.activate = [this, k, i, most, best] {
                    pad_.open("Best time", best == menu::kNoTime ? most : best, 1'000, most, true, [this, k, i](s64 v) {
                        if (editor_->set_time(editor_->licenses()[k].tests[i], u32(v))) changed();
                    });
                };
                r.cross_hint = "type the best time";
            }
        }
    }

    // ---- Events --------------------------------------------------------------------------------------------------------
    void build_events() {
        link("Every event", "", "Win or clear all of them.", [this] {
            confirm_.open("Every race of every event", {{"Win every race", [this] { editor_->win_all(true), changed(); }},
                                                        {"Clear every result", [this] { editor_->win_all(false), changed(); }},
                                                        {"Leave them", [] {}}});
        });
        for (size_t i = 0; i < editor_->events().size(); ++i) {
            const SaveEditor::EventList& l = editor_->events()[i];
            int won = 0;
            for (const SaveEditor::Event& e : l.events) won += editor_->won(e);
            Row& r = link(l.title, std::to_string(won) + " of " + std::to_string(l.events.size()) + " won", "", [this, i] { push(editor_->events()[i].title, [this, i] { build_event_list(i); }); });
            r.accent = won == int(l.events.size());
        }
    }
    std::string event_state(const SaveEditor::Event& e) {
        if (editor_->won(e)) return "Won";
        if (!editor_->entered(e)) return "Not run";
        int won = 0;
        for (const SaveEditor::EventRace& r : e.races) won += editor_->place(r.unit) == 0;
        return std::to_string(won) + " of " + times(int(e.races.size()), "race", "races") + " won";
    }
    void build_event_list(size_t l) {
        const SaveEditor::EventList& list = editor_->events()[l];
        link("Every event here", "", "Win or clear all of them.", [this, l] {
            auto all = [this, l](bool on) {
                for (const SaveEditor::Event& e : editor_->events()[l].events) editor_->set_won(e, on);
                changed();
            };
            confirm_.open(editor_->events()[l].title, {{"Win every race", [all] { all(true); }}, {"Clear every result", [all] { all(false); }}, {"Leave them", [] {}}});
        });
        for (size_t i = 0; i < list.events.size(); ++i) {
            const SaveEditor::Event& e = list.events[i];
            Row& r = link(e.name, event_state(e), e.championship ? "A championship of " + times(int(e.races.size()), "race", "races") + "." : times(int(e.races.size()), "race", "races") + ".",
                          [this, l, i] { push(editor_->events()[l].events[i].name, [this, l, i] { build_event(l, i); }); });
            r.accent = editor_->won(e);
            if (!e.prize.empty()) r.hint += " Prize car: " + editor_->car_name(e.prize) + ".", r.car = e.prize;
        }
    }
    std::vector<std::string> place_names(int now) {
        std::vector<std::string> out{"Not run"};
        for (int p = 0; p < 6; ++p) out.push_back(ordinal(p) + (p == 0 ? ", gold" : p == 1 ? ", silver" : p == 2 ? ", bronze" : ""));
        if (now == SaveEditor::kOther) out.push_back("Run, no place");
        return out;
    }
    void build_event(size_t l, size_t i) {
        const SaveEditor::Event& e = editor_->events()[l].events[i];
        link(editor_->won(e) ? "Clear this event" : "Win this event", event_state(e), e.championship ? "Every race of it and the championship." : "Every race of it.", [this, l, i] {
            const SaveEditor::Event& ev = editor_->events()[l].events[i];
            editor_->set_won(ev, !editor_->won(ev)), changed();
        }).cross_hint = "choose";
        if (e.championship) {
            const int p = editor_->place(e.unit);
            choice("The championship", {"Not won", "Won"}, p == 0, [this, l, i](int v) { editor_->set_series(editor_->events()[l].events[i], v ? 0 : SaveEditor::kNotRun); });
        }
        if (!e.prize.empty()) {
            Row& r = choice("Prize car: " + editor_->car_name(e.prize), {"Not waiting", "Waiting"}, editor_->prize_waiting(e), [this, l, i](int v) { editor_->set_prize_waiting(editor_->events()[l].events[i], v == 1); },
                            "A waiting prize car is given when you next go to Gran Turismo mode, or on the Prizes page here.");
            r.enabled = editor_->won(e), r.car = e.prize;
        }
        heading(e.championship ? "Races (the best place in each)" : "Races");
        for (size_t k = 0; k < e.races.size(); ++k) {
            const int now = editor_->place(e.races[k].unit);
            const std::vector<std::string> names = place_names(now);
            Row& r = choice(std::to_string(k + 1) + ". " + e.races[k].name, names, now == SaveEditor::kOther ? 7 : now + 1, [this, l, i, k](int v) {
                if (v < 7) editor_->set_place(editor_->events()[l].events[i], int(k), v - 1);
            });
            r.accent = now == 0;
        }
        heading("A-spec points (the best of each race)");
        for (const SaveEditor::EventRace& race : e.races) score_row(race.name, race.unit);
        if (e.championship) score_row("The championship", e.unit);
    }
    void score_row(const std::string& name, int unit) {
        const int now = editor_->score(unit);
        Row& r = number(name, now, 0, std::max(SaveEditor::kMaxScore, now), 10, [this, unit](s64 v) { editor_->set_score(unit, int(v)); }, "", [](s64 v) { return std::to_string(v) + " pts"; });
        r.enabled = editor_->place(unit) != SaveEditor::kNotRun;
    }

    // ---- Missions ------------------------------------------------------------------------------------------------------
    void build_missions() {
        link("Every mission", "", "First in all of them, or none run.", [this] {
            auto all = [this](int place) {
                for (const SaveEditor::Mission& m : editor_->missions()) editor_->set_mission(m, place);
                changed();
            };
            confirm_.open("Every driving mission", {{"First in every mission", [all] { all(0); }}, {"Clear every result", [all] { all(SaveEditor::kNotRun); }}, {"Leave them", [] {}}});
        });
        for (size_t i = 0; i < editor_->missions().size(); ++i) {
            const SaveEditor::Mission& m = editor_->missions()[i];
            const int now = editor_->place(m.unit);
            Row& r = choice(std::to_string(i + 1) + ". " + m.name, place_names(now), now == SaveEditor::kOther ? 7 : now + 1, [this, i](int v) {
                if (v < 7) editor_->set_mission(editor_->missions()[i], v - 1);
            }, m.course);
            r.accent = now == 0;
            r.activate = [this, i] {
                const int unit = editor_->missions()[i].unit;
                if (editor_->place(unit) == SaveEditor::kNotRun) return;
                pad_.open("A-spec points", editor_->score(unit), 0, std::max(SaveEditor::kMaxScore, editor_->score(unit)), false, [this, unit](s64 v) { editor_->set_score(unit, int(v)), changed(); });
            };
            r.hint += ". A-spec points: " + std::to_string(editor_->score(m.unit)) + ".";
            r.cross_hint = "type the A-spec points";
        }
    }

    // ---- Prizes --------------------------------------------------------------------------------------------------------
    void build_prizes() {
        const int waiting = editor_->presents_waiting();
        Row& r = link("Receive the prize cars earned", waiting ? times(waiting, "car", "cars") : "none waiting", "They go into the garage now, a day passing for each as in the game. Else the game gives them when you next go to Gran Turismo mode.", [this] {
            bool full = false;
            const int got = editor_->receive_presents(&full);
            changed();
            done(times(got, "prize car", "prize cars") + " received." + (full ? " The garage is full: the rest could not be kept." : ""));
        });
        r.enabled = waiting > 0, r.cross_hint = "receive them";
        int events = 0, set = 0;
        for (const SaveEditor::EventList& l : editor_->events())
            for (const SaveEditor::Event& e : l.events)
                if (!e.prize.empty() && editor_->won(e)) ++events, set += editor_->prize_waiting(e);
        Row& w = link("Prize cars of the events won", std::to_string(set) + " of " + std::to_string(events) + " waiting", "Have the prize car of every event won waiting again.", [this] { editor_->all_prizes_waiting(), changed(); });
        w.enabled = set < events, w.cross_hint = "choose";
        heading("Given once each");
        for (size_t i = 0; i < editor_->presents().size(); ++i) {
            const SaveEditor::Present& p = editor_->presents()[i];
            const bool given = editor_->present_given(p);
            const bool earned = !given && editor_->present_earned(p);
            Row& row = choice(p.what, {earned ? "Earned, waiting" : "Not given yet", "Given"}, given, [this, i](int v) { editor_->set_present_given(editor_->presents()[i], v == 1); }, "For: " + p.how + ".");
            row.accent = earned, row.car = p.car;
        }
    }

    // ---- Records -------------------------------------------------------------------------------------------------------
    void build_records() {
        std::vector<SaveEditor::CourseRecord> all = editor_->course_records();
        int shown = 0;
        for (const SaveEditor::CourseRecord& r : all) {
            if (r.entries.empty()) continue;
            ++shown;
            link(r.name, record_value(r, 0), times(int(r.entries.size()), "time", "times") + " kept.", [this, at = r.at, machine = r.machine, name = r.name] { push(name, [this, at, machine] { build_record(at, machine); }); });
        }
        if (!shown) info("No times are kept yet", "");
    }
    std::string record_value(const SaveEditor::CourseRecord& r, size_t i) {
        if (i >= r.entries.size()) return "";
        if (!r.speed) return time_text(r.entries[i].value);
        float v;
        std::memcpy(&v, &r.entries[i].value, 4);
        const GameState::Unit u = state().velocity_unit();
        return std::to_string(int(v * 3.6f * u.factor)) + " " + u.name;
    }
    void build_record(size_t at, bool machine) {
        for (const SaveEditor::CourseRecord& r : editor_->course_records()) {
            if (r.at != at || r.machine != machine) continue;
            for (size_t i = 0; i < r.entries.size(); ++i) {
                Row& row = link(std::to_string(i + 1) + ". " + (r.entries[i].car.empty() ? "-" : r.entries[i].car), record_value(r, i), r.entries[i].date.empty() ? "" : "Set on " + r.entries[i].date + ".", [this, r, i] {
                    confirm_.open("Delete this time?", {{"Delete it", [this, r, i] { editor_->delete_entry(r, i), changed(); }}, {"Keep it", [] {}}});
                });
                row.cross_hint = "delete it";
            }
            if (r.entries.empty()) info("No times are kept", "");
            else
                link("Delete every time", "", "", [this, r] {
                    confirm_.open("Delete every time of " + r.name + "?", {{"Delete them", [this, r] { editor_->clear_record(r), changed(); }}, {"Keep them", [] {}}});
                });
        }
    }

    // ---- Arcade --------------------------------------------------------------------------------------------------------
    void build_arcade() {
        choice("Everything in Arcade mode", {"As opened below", "All open"}, state().all_available, [this](int i) { state().all_available = i == 1; }, "Every car and course, whatever is opened below.");
        const int cars = int(editor_->cars().size());
        link("Cars", with_commas(s64(editor_->cars_open())) + " of " + with_commas(cars) + " open", "", [this] { push("Cars in Arcade mode", [this] { build_arcade_cars(); }); });
        int open = 0;
        for (const SaveEditor::Course& c : editor_->courses()) open += editor_->course_open(c);
        link("Courses and photo places", std::to_string(open) + " of " + std::to_string(editor_->courses().size()) + " open", "In Arcade and Gran Turismo mode.", [this] { push("Courses", [this] { build_courses(); }); });
    }
    void build_courses() {
        link("Every course", "", "", [this] {
            confirm_.open("Every course and photo place", {{"Open them all", [this] { editor_->open_all_courses(true), changed(); }}, {"Close them all", [this] { editor_->open_all_courses(false), changed(); }}, {"Leave them", [] {}}});
        });
        for (size_t i = 0; i < editor_->courses().size(); ++i) {
            const SaveEditor::Course& c = editor_->courses()[i];
            Row& r = choice(c.name, {"Closed", "Open"}, editor_->course_open(c), [this, i](int v) { editor_->set_course_open(editor_->courses()[i], v == 1); });
            r.accent = editor_->course_open(c);
        }
    }
    void build_arcade_cars() {
        link("Every car", "", "", [this] {
            confirm_.open("Every car in Arcade mode", {{"Open them all", [this] { editor_->open_all_cars(true), changed(); }}, {"Only the garage's cars", [this] { editor_->open_all_cars(false), changed(); }}, {"Leave them", [] {}}});
        });
        std::map<std::string, std::pair<int, int>> makers; // open, all
        for (const SaveEditor::CarKind& k : editor_->cars()) ++makers[k.maker].second, makers[k.maker].first += int(state().available_cars.count(k.label));
        for (const auto& [m, n] : makers) link(m, std::to_string(n.first) + " of " + std::to_string(n.second) + " open", "", [this, m = m] { push(m, [this, m] {
                for (const SaveEditor::CarKind& k : editor_->cars())
                    if (k.maker == m) {
                        const bool on = state().available_cars.count(k.label) > 0;
                        choice(k.name, {"Closed", "Open"}, on, [this, label = k.label](int v) { editor_->set_car_open(label, v == 1); }).accent = on;
                        rows_.back().car = k.label;
                    }
            }); });
    }

    // ---- Prologue ------------------------------------------------------------------------------------------------------
    void build_prologue() {
        menu::PrologueSaveEditor& e = *prologue_editor_;
        link("Name", e.state().username, "The letters your results are entered under.", [this] { text_.open("Name", prologue_editor_->state().username, menu::PrologueSaveEditor::kNameLetters, [this](const std::string& v) { prologue_editor_->set_name(v), changed(); }); });
        int n[4] = {};
        for (const auto& t : e.tests()) ++n[t.medal];
        info("License tests", std::to_string(n[3]) + " gold, " + std::to_string(n[2]) + " silver, " + std::to_string(n[1]) + " bronze of " + std::to_string(e.tests().size()));
        info("Prologue keeps no money, garage or calendar", "");
    }
    void build_prologue_licenses() {
        link("Every test", "", "One medal for all tests.", [this] {
            picker_.open("Every test", {{"Gold", ""}, {"Silver", ""}, {"Bronze", ""}, {"No medal", ""}}, 0, [this](int i) {
                for (size_t k = 0; k < prologue_editor_->tests().size(); ++k) prologue_editor_->set_medal(k, 3 - i);
                changed();
            });
        });
        for (size_t k = 0; k < prologue_editor_->tests().size(); ++k) {
            const menu::PrologueSaveEditor::Test& t = prologue_editor_->tests()[k];
            std::vector<std::string> names;
            for (int m = 0; m < 4; ++m) names.push_back(medal_name(m) + (t.best == menu::kNoTime ? "" : ", " + time_text(t.best)));
            std::string hint;
            for (int m = 3; m >= 1; --m)
                if (t.limit[m]) hint += (hint.empty() ? "" : ", ") + medal_name(m) + " " + time_text(t.limit[m]);
            choice(t.name, names, t.medal, [this, k](int m) { prologue_editor_->set_medal(k, m); }, hint).accent = t.medal == 3;
        }
    }
};

} // namespace

std::unique_ptr<Screen> make_save_editor(App& app, const std::string& card) { return std::make_unique<SaveEditorScreen>(app, card); }

} // namespace gt4r
