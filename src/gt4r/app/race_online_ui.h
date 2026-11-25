#pragma once

// What an online race shows beside the race's own display (docs/specs/net.md "The race's pages"): the players' names over
// their cars, the list of the players, the messages, the pause window (the race goes on under it), what the results wait for.

#include "gt4r/app/app.h"

#include <string>
#include <vector>

namespace gt4r {

namespace net {
class NetRace;
}

class OnlineRaceUi {
public:
    // A car's place in the picture: where its name goes (Ui units), how far it is (m); drawn when `seen`.
    struct Label {
        size_t car = 0;
        float x = 0, y = 0, distance = 0;
        bool seen = false;
    };
    // Each frame: the race's messages taken, the old ones gone.
    void frame(net::NetRace& net, double dt);
    // Over the race display. `places`: each car's place (1 the leader), `laps` done.
    void draw_race(Ui& ui, const net::NetRace& net, const std::vector<Label>& labels, const std::vector<int>& places);
    // Before the start: the players the race waits for.
    void draw_waiting(Ui& ui, const net::NetRace& net);
    // The pause window. `host`: leaving ends the race for everyone once nobody races.
    void draw_pause(Ui& ui, bool host, bool leaving);
    // Under the results: what they wait for, and the keys.
    void draw_results_hint(Ui& ui, const net::NetRace& net, bool host);
    // A race left before its end: the others' list, until the host goes back.
    void draw_left(Ui& ui, const net::NetRace& net, bool host);

private:
    struct Message {
        std::string text;
        double left = 4;
    };
    std::vector<Message> messages_;
};

} // namespace gt4r
