#include "gt4r/app/race_online_ui.h"

#include "gt4r/net/net_race.h"
#include "gt4r/race/race_display.h"
#include "gt4r/race/race_flow.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace gt4r {

namespace {

// Text over the race's picture: its dark copy under it.
void shadowed(Ui& ui, float x, float y, const std::string& s, Color c) {
    ui.text(x + 1, y + 1, s, Color{0, 0, 0, u8(c.a * 3 / 4)});
    ui.text(x, y, s, c);
}

std::string racing_names(const net::NetRace& net) {
    std::string out;
    for (const net::NetRace::Standing& s : net.standings())
        if (!s.finished && !s.retired && !s.ours) out += (out.empty() ? "" : ", ") + s.player;
    return out;
}

} // namespace

void OnlineRaceUi::frame(net::NetRace& net, double dt) {
    for (std::string& t : net.take_messages()) messages_.push_back({std::move(t), 4.0});
    for (Message& m : messages_) m.left -= dt;
    std::erase_if(messages_, [](const Message& m) { return m.left <= 0; });
    if (messages_.size() > 4) messages_.erase(messages_.begin(), messages_.end() - 4);
}

void OnlineRaceUi::draw_race(Ui& ui, const net::NetRace& net, const std::vector<Label>& labels, const std::vector<int>& places) {
    const std::vector<net::NetRace::Standing> cars = net.standings();
    // The names, the far ones first.
    std::vector<Label> order = labels;
    std::stable_sort(order.begin(), order.end(), [](const Label& a, const Label& b) { return a.distance > b.distance; });
    for (const Label& l : order) {
        if (!l.seen || l.car >= cars.size() || cars[l.car].ours || cars[l.car].retired || l.distance > 150) continue;
        const net::NetRace::Standing& c = cars[l.car];
        const float fade = std::clamp((150 - l.distance) / 40, 0.0f, 1.0f);
        const float w = Ui::text_width(c.player) + 8;
        const u8 a = u8((c.ghost ? 110 : 220) * fade);
        ui.fill_round(l.x - w / 2, l.y - 13, w, 13, 3, Color{0, 0, 0, u8(a / 2)});
        ui.text(l.x - w / 2 + 4, l.y - 12, c.player, Color{255, 255, 255, a});
    }
    // The players in race order.
    std::vector<size_t> by_place(cars.size());
    std::iota(by_place.begin(), by_place.end(), size_t(0));
    std::stable_sort(by_place.begin(), by_place.end(), [&](size_t a, size_t b) { return a < places.size() && b < places.size() && places[a] < places[b]; });
    float y = 122;
    for (size_t k = 0; k < by_place.size(); ++k) {
        const net::NetRace::Standing& c = cars[by_place[k]];
        const Color color = c.retired ? Color{150, 150, 150, 160} : c.ours ? Color{255, 214, 90, 235} : Color{255, 255, 255, 220};
        // (with the map's letter: the car's entry)
        std::string line = std::to_string(k + 1) + "  " + Ui::elide(c.player, 92) + (by_place[k] < net.seats().size() ? std::string("  ") + char('A' + std::min<size_t>(net.seats()[by_place[k]], 25)) : std::string());
        if (c.retired) line += "  out";
        else if (c.finished) line += "  finished";
        else if (c.ghost) line += "  (no contact)";
        shadowed(ui, Ui::kWidth - 8 - Ui::text_width(line), y, line, color);
        y += 12;
    }
    float my = 262;
    for (const Message& m : messages_) {
        const u8 a = u8(255 * std::clamp(m.left * 2, 0.0, 1.0));
        ui.text_center(my + 1, m.text, Color{0, 0, 0, u8(a * 3 / 4)});
        ui.text_center(my, m.text, Color{255, 255, 255, a});
        my += 13;
    }
}

void OnlineRaceUi::draw_waiting(Ui& ui, const net::NetRace& net) {
    if (net.phase() == net::NetRace::Phase::Racing || net.phase() == net::NetRace::Phase::Over || (net.phase() == net::NetRace::Phase::Ready && net.start_in() < 0.3)) return;
    const char* text = net.phase() == net::NetRace::Phase::Loading ? "Waiting for the other players..." : "Everyone is here.";
    const float w = Ui::text_width(text) + 24;
    ui.fill_round((Ui::kWidth - w) / 2, 250, w, 20, 5, Color{0, 0, 0, 160});
    ui.text_center(254, text, colors::kText);
}

void OnlineRaceUi::draw_pause(Ui& ui, bool host, bool leaving) {
    const float w = 340, h = 84, x = (Ui::kWidth - w) / 2, y = 130;
    ui.fill_round(x, y, w, h, 6, Color{12, 14, 20, 225});
    ui.frame_round(x, y, w, h, 6, 1, colors::kOutline);
    ui.text_center(y + 10, "The race goes on", colors::kAccent);
    if (leaving) {
        ui.text_center(y + 30, host ? "Leave the race? The others race on; the lobby stays." : "Leave the race? The lobby stays.", colors::kText);
        ui.text_center(y + 58, "{Cross}: leave   {Triangle}: race on", colors::kDim);
    } else {
        ui.text_center(y + 30, "It stops for nobody: the car rolls on without you.", colors::kDim);
        ui.text_center(y + 58, "{Start}: race on   {Square}: leave the race", colors::kDim);
    }
}

void OnlineRaceUi::draw_results_hint(Ui& ui, const net::NetRace& net, bool host) {
    std::string line;
    if (net.lost()) line = "The connection was lost.   {Cross}: back to the menus";
    else if (net.all_done()) line = host ? "{Cross}: everyone back to the lobby" : "The host takes everyone back to the lobby.   {Cross}: wait in the menus";
    else line = "Still racing: " + racing_names(net) + (host ? "   {Square}: end the race now" : "   {Cross}: wait in the menus");
    ui.fill(0, Ui::kHeight - 18, Ui::kWidth, 18, Color{0, 0, 0, 170});
    ui.text_center(Ui::kHeight - 14, line, colors::kText);
}

void OnlineRaceUi::draw_left(Ui& ui, const net::NetRace& net, bool host) {
    std::vector<net::NetRace::Standing> cars = net.standings();
    std::stable_sort(cars.begin(), cars.end(), [](const auto& a, const auto& b) {
        if (a.finished != b.finished) return a.finished;
        return a.finished && a.time_units < b.time_units;
    });
    const float w = 360, h = 44 + 16 * float(cars.size()), x = (Ui::kWidth - w) / 2, y = 90;
    ui.fill_round(x, y, w, h, 6, Color{12, 14, 20, 215});
    ui.text_center(y + 10, "You left the race", colors::kAccent);
    for (size_t i = 0; i < cars.size(); ++i) {
        const float ry = y + 32 + 16 * float(i);
        ui.text(x + 14, ry, cars[i].player, cars[i].ours ? colors::kAccent : colors::kText);
        ui.text_right(x + w - 14, ry, cars[i].finished ? race::format_lap_time(cars[i].time_units / 3) : cars[i].retired ? std::string("out") : std::string("racing"), colors::kDim);
    }
    draw_results_hint(ui, net, host);
}

} // namespace gt4r
