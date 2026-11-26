#include "gt4r/app/updates.h"

#include "gt4r/app/app.h"
#include "gt4r/app/online.h"

#include "base/log.h"
#include "base/config.h"
#include "net/session.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace gt4r {

namespace {

namespace rv = gt3r::net;
using Prompt = gt3r::net::UpdatePrompt;

std::string utc_now() {
    char t[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(t, sizeof t, "%Y-%m-%d %H:%M UTC", std::gmtime(&now));
    return t;
}

net::GameVersion read_version(const gt3r::IniDocument& cfg, const char* key) {
    std::optional<net::GameVersion> v = rv::parse_game_version(cfg.get("online", key).value_or(""));
    if (!v) return {};
    v->suffix.clear(); // releases only
    return *v;
}

bool web_url(const std::string& u) { return u.rfind("https://", 0) == 0 || u.rfind("http://", 0) == 0; }

const net::GameVersion& ours() { return net::this_version(); }

} // namespace

Updates& updates() {
    static Updates u;
    return u;
}

bool Updates::enabled(App& app) { return gt3r::to_lower(app.config().get("online", "update_check").value_or("on")) != "off"; }

void Updates::set_enabled(App& app, bool on) {
    app.config().set("online", "update_check", on ? "on" : "off");
    app.save_config();
}

std::string Updates::server(App& app) {
    const std::string s = std::string(gt3r::trim(app.config().get("online", "server").value_or("")));
    return s.empty() ? std::string(kOfferedServer) : s;
}

void Updates::startup(App& app, bool network) {
    const gt3r::IniDocument& cfg = app.config();
    latest_ = read_version(cfg, "update_latest");
    min_online_ = read_version(cfg, "update_min_online");
    dismissed_ = read_version(cfg, "update_dismissed");
    url_ = cfg.get("online", "update_url").value_or("");
    if (!web_url(url_)) url_.clear();
    checked_ = cfg.get("online", "update_checked").value_or("");
    update_due(); // the answer kept may already call for the prompt
    // Only a server the player has chosen is asked at startup (the online page's promise: none is contacted before).
    const std::string chosen = std::string(gt3r::trim(cfg.get("online", "server").value_or("")));
    if (network && enabled(app) && !chosen.empty()) start(app, chosen);
}

void Updates::check_now(App& app) { start(app, server(app)); }

void Updates::start(App& app, const std::string& server) {
    (void)app;
    state_ = State::Checking;
    error_.clear();
    check_.start(server, ours(), rv::this_platform());
}

void Updates::poll(App& app) {
    std::optional<net::VersionCheck::Result> r = check_.take();
    if (!r) return;
    if (!r->answered) {
        state_ = State::Failed;
        error_ = r->error;
        LOG_INFO("Update check: %s", error_.c_str());
        return;
    }
    set_answer(app, r->answer);
    LOG_INFO("Update check: this is %s; latest %s, online play needs %s%s", ours().to_string().c_str(), latest_.known() ? latest_.number().c_str() : "(none)",
             min_online_.known() ? min_online_.number().c_str() : "any", required() ? " (update required)" : available() ? " (update available)" : "");
}

void Updates::set_answer(App& app, const net::VersionAnswer& a) {
    state_ = State::Done;
    latest_ = a.latest, latest_.suffix.clear();
    min_online_ = a.min_online, min_online_.suffix.clear();
    url_ = web_url(a.url) ? a.url : std::string();
    checked_ = utc_now();
    store(app);
    update_due();
}

// The startup prompt: once per release (not after Later for it, not twice in one run).
void Updates::update_due() {
    const net::GameVersion v = rv::prompt_version(latest_, min_online_);
    startup_due_ = rv::show_startup_prompt(ours(), latest_, min_online_, dismissed_) && (!shown_.known() || rv::compare_core(v, shown_) > 0);
}

void Updates::store(App& app) {
    gt3r::IniDocument& cfg = app.config();
    cfg.set("online", "update_latest", latest_.known() ? latest_.number() : "");
    cfg.set("online", "update_min_online", min_online_.known() ? min_online_.number() : "");
    cfg.set("online", "update_url", url_);
    cfg.set("online", "update_checked", checked_);
    app.save_config();
}

bool Updates::available() const { return rv::update_available(ours(), latest_); }
bool Updates::required() const { return !rv::meets_minimum(ours(), min_online_); }
Prompt Updates::prompt() const { return rv::update_prompt(ours(), latest_, min_online_); }
gt3r::net::UpdateLinks Updates::links() const { return rv::update_links(url_, net::this_release_asset()); }

std::string Updates::status_text() const {
    switch (state_) {
    case State::Checking: return "checking...";
    case State::Failed: return "could not check (" + error_ + ")";
    default: break;
    }
    if (!known()) return checked_.empty() ? "not checked yet" : "no release announced";
    if (required()) return "update required to play online: version " + latest_.number();
    if (available()) return "version " + latest_.number() + " is available";
    return "up to date";
}

namespace {

bool open_url(const std::string& url) {
    if (url.empty()) return false;
    if (!SDL_OpenURL(url.c_str())) {
        LOG_WARN("Could not open %s: %s", url.c_str(), SDL_GetError());
        return false;
    }
    LOG_INFO("Opened %s", url.c_str());
    return true;
}

bool scripted_run = false; // (set by App each frame: a scripted run opens no browser)

} // namespace

bool Updates::open_download() const {
    if (!scripted_run) return open_url(links().download);
    LOG_INFO("Scripted run: not opening %s", links().download.c_str());
    return true;
}

bool Updates::open_notes() const {
    if (!scripted_run) return open_url(links().notes);
    LOG_INFO("Scripted run: not opening %s", links().notes.c_str());
    return true;
}

// --- the dialog ---

void Updates::open_dialog() {
    const Prompt kind = prompt();
    if (kind == Prompt::None) return;
    if (!dialog_.open) dialog_.focus = 0; // Download update
    dialog_.open = true;
    dialog_.kind = kind;
    shown_ = rv::prompt_version(latest_, min_online_);
    startup_due_ = false;
}

void Updates::frame(App& app, bool screen_allows) {
    scripted_run = app.scripted();
    if (startup_due_ && screen_allows && !modal() && app.frame() >= 2) { // (over a screen that has run: its page drawn behind)
        LOG_INFO("Update dialog: %s (frame %d)", prompt() == Prompt::Required ? "update required" : "update available", app.frame());
        open_dialog();
    }
}

void Updates::close_dialog(App& app, bool later) {
    if (later) { // not again at startup until a newer release
        dismissed_ = rv::prompt_version(latest_, min_online_);
        app.config().set("online", "update_dismissed", dismissed_.known() ? dismissed_.number() : "");
        app.save_config();
    }
    dialog_.open = false;
    dialog_.wait_release = true;
}

void Updates::activate(App& app, int item) {
    switch (item) {
    case 0: open_download(), close_dialog(app, false); break; // Download update
    case 1: close_dialog(app, true); break;                     // Later
    case 2: open_notes(); break;                                // Release notes (the dialog stays)
    default: break;
    }
}

void Updates::dialog_update(App& app) {
    const gt3r::MenuInput& m = app.menu();
    if (!dialog_.open) { // closed: until its button is let go
        bool held = false;
        for (PadButton b : {PadButton::Cross, PadButton::Circle, PadButton::Triangle, PadButton::Square, PadButton::Start}) held = held || m.held(b);
        if (!held) dialog_.wait_release = false;
        return;
    }
    const gt3r::net::UpdateLinks l = links();
    const bool notes = !l.notes.empty() && l.notes != l.download;
    for (const gt3r::PointerInput::Action& a : app.pointer()) {
        int hit = -1;
        for (int i = 0; i < 3; ++i)
            if (dialog_.items[i].w > 0 && dialog_.items[i].contains(a.x, a.y)) hit = i;
        if (hit < 0) continue;
        if (a.kind == gt3r::PointerInput::Action::Kind::Hover) dialog_.focus = hit;
        else if (a.kind == gt3r::PointerInput::Action::Kind::Click) {
            dialog_.focus = hit;
            return activate(app, hit);
        }
    }
    if (const int h = m.horizontal(); h && dialog_.focus < 2) dialog_.focus = h > 0 ? 1 : 0;
    if (const int v = m.vertical()) dialog_.focus = v > 0 && notes ? 2 : v < 0 && dialog_.focus == 2 ? 0 : dialog_.focus;
    if (m.pressed(PadButton::Cross) || m.pressed(PadButton::Start)) activate(app, dialog_.focus);
    else if (m.pressed(PadButton::Circle) || m.pressed(PadButton::Triangle)) activate(app, 1);
}

void Updates::dialog_render(Ui& ui) {
    for (gt3r::UiRect& r : dialog_.items) r = {};
    if (!dialog_.open) return;
    const bool req = dialog_.kind == Prompt::Required;
    const std::string mine = ours().to_string();
    const std::string title = req ? "Update required to play online" : "Update available";
    const net::GameVersion need = min_online_.known() ? min_online_ : latest_;
    const std::string body = req ? "Online play needs GT4 Revisited " + need.number() + " or newer. You have " + mine + ". Offline play is not affected."
                                 : "GT4 Revisited " + latest_.number() + " is available. You have " + mine + ".";
    const gt3r::net::UpdateLinks l = links();
    const bool notes = !l.notes.empty() && l.notes != l.download;

    ui.fill(0, 0, ui.width(), ui.height(), Color{0, 0, 0, 160}); // modal: the screen behind dims
    const float w = std::floor(std::min(ui.width() - 32, 340.0f));
    const float pad = 14, bh = 24, lh = 16;
    const std::vector<std::string> lines = Ui::wrap(body, w - 2 * pad);
    const float h = pad + 2 * Ui::kTitleCap + 12 + 11 * float(lines.size()) + 12 + bh + (notes ? 8 + lh : 0) + pad;
    const float x = std::floor((ui.width() - w) / 2), y = std::floor((ui.height() - h) / 2);
    ui.fill_round(x, y, w, h, 6, colors::kPanel);
    ui.frame_round(x, y, w, h, 6, 1, req ? colors::kBad : colors::kOutline);
    float ty = y + pad;
    ui.text_big(x + pad, ty, title, 2, req ? colors::kBad : colors::kAccent);
    ty += 2 * Ui::kTitleCap + 12;
    for (const std::string& line : lines) {
        ui.text(x + pad, ty, line, colors::kText);
        ty += 11;
    }
    ty += 12 - 11 + Ui::kChar;
    const float bw = std::floor((w - 2 * pad - 8) / 2);
    dialog_.items[0] = {x + pad, ty, bw, bh};
    dialog_.items[1] = {x + pad + bw + 8, ty, bw, bh};
    ui.button(dialog_.items[0], "Download update", dialog_.focus == 0, false, true);
    ui.button(dialog_.items[1], "Later", dialog_.focus == 1, false);
    if (notes) {
        const char* label = "Release notes";
        const float tw = Ui::text_width(label) + 16;
        dialog_.items[2] = {std::floor(x + (w - tw) / 2), ty + bh + 8, tw, lh};
        const gt3r::UiRect& r = dialog_.items[2];
        const bool f = dialog_.focus == 2;
        if (f) ui.fill_round(r.x, r.y, r.w, r.h, 4, colors::kHighlight);
        const float cy = std::floor(r.y + (r.h - Ui::kChar) / 2);
        ui.text_center_in(r.x, r.w, cy, label, f ? colors::kAccent : colors::kDim);
        ui.fill(r.x + 8, cy + Ui::kChar + 2, r.w - 16, 1, f ? colors::kAccent : colors::kDim); // a link
    }
}

std::string Updates::badge_text() const {
    switch (prompt()) {
    case Prompt::Required: return "Update required for online";
    case Prompt::Available: return "Update available";
    default: return "";
    }
}

} // namespace gt4r
