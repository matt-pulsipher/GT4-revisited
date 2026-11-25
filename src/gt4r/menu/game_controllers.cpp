// MOption's controller settings (0x18FD50 .. 0x191B00): the option pages' Key Configuration, the pad's vibration and the
// steering wheel's forces, kept in the options as the saved game's block (input/key_config.h; GameState::options
// "controllers_1p" / "controllers_2p").

#include "gt4r/input/key_config.h"
#include "gt4r/menu/game_internal.h"

namespace gt4r::menu {

namespace {

using script::Args;
using script::Value;
using script::Vm;
using Kind = script::Kind;

std::string option_name(int port) { return port == 1 ? "controllers_2p" : "controllers_1p"; }
input::PortConfig port_of(const GameState& s, int port) { return input::port_from_text(s.option_text(option_name(port))); }
void set_port(GameState& s, int port, const input::PortConfig& c) { s.options[option_name(port)] = input::port_text(c); }

// A call's port and device: (port, device name, ...) for the keys (0x1908C0).
struct Target {
    int port = 0;
    int device = -1;
};
Target key_target(Args a) { return {arg_int(a, 0), input::device_index(arg_text(a, 1))}; }
// The presets' and feedbacks' calls name the device first, the port last; whatever the order, the first text is the device.
Target named_target(Args a, int skip_numbers) {
    Target t;
    for (const Value& v : a) {
        if (v.kind == Kind::String) {
            if (t.device < 0) t.device = input::device_index(v.as_string());
        } else if (skip_numbers-- <= 0) t.port = int(v.as_float());
    }
    return t;
}

} // namespace

void install_controllers(GameNatives& n, Object& option) {
    GameState* s = &n.state();
    for (int port = 0; port < 2; ++port) {
        const std::string who = port == 0 ? "_1p" : "_2p";
        for (const auto& [name, key] : {std::pair<const char*, const char*>{"vibration", "VIBRATION"}, {"steering_forcefeedback", "WHEEL FEEDBACK"}})
            option.property(name + who, [s, port, key = key] { return number(input::get_option(port_of(*s, port), key)); },
                            [s, port, key = key](const Value& v) {
                                input::PortConfig c = port_of(*s, port);
                                input::set_option(c, key, v.truthy());
                                set_port(*s, port, c);
                            });
    }
    // The keys of a device: their actions by name.
    const auto edit = [s](Args a, auto&& change) {
        const Target t = key_target(a);
        if (t.device < 0 || t.port < 0 || t.port > 1) return;
        input::PortConfig c = port_of(*s, t.port);
        change(c.device[size_t(t.device)], input::Device(t.device));
        set_port(*s, t.port, c);
    };
    const auto read = [s](Args a, auto&& of) {
        const Target t = key_target(a);
        if (t.device < 0 || t.port < 0 || t.port > 1) return Value{};
        const input::PortConfig c = port_of(*s, t.port);
        return of(c.device[size_t(t.device)], input::Device(t.device));
    };
    const auto keys = [](const Value& v, auto&& each) {
        if (const std::vector<Value>* list = v.items())
            for (const Value& k : *list) each(k.as_string());
    };
    const auto list_of = [](const std::vector<std::string>& names) {
        std::vector<Value> out;
        for (const std::string& k : names) out.push_back(text(k));
        return Value::array(std::move(out));
    };
    // (0x190A00, 0x191310: every key with the action, none)
    option.method("clearAnalogFunction", [edit](Vm&, const Value&, Args a) {
        edit(a, [&](input::DeviceConfig& c, input::Device d) {
            for (const std::string& k : input::keys_with(c, d, input::axis_action(arg_text(a, 2)))) input::set_axis(c, d, k, input::AxisAction::None);
        });
        return Value{};
    });
    option.method("clearButtonFunction", [edit](Vm&, const Value&, Args a) {
        edit(a, [&](input::DeviceConfig& c, input::Device d) {
            for (const std::string& k : input::keys_with(c, d, input::button_action(arg_text(a, 2)))) input::set_button(c, d, k, input::ButtonAction::None);
        });
        return Value{};
    });
    // (0x190B50, 0x191460: the listed keys that have the action, none)
    option.method("unsetAnalogKey", [edit, keys](Vm&, const Value&, Args a) {
        edit(a, [&](input::DeviceConfig& c, input::Device d) {
            const input::AxisAction f = input::axis_action(arg_text(a, 2));
            if (a.size() > 3) keys(a[3], [&](const std::string& k) {
                if (input::get_axis(c, d, k) == gt3r::s8(f)) input::set_axis(c, d, k, input::AxisAction::None);
            });
        });
        return Value{};
    });
    option.method("unsetButtonKey", [edit, keys](Vm&, const Value&, Args a) {
        edit(a, [&](input::DeviceConfig& c, input::Device d) {
            const input::ButtonAction f = input::button_action(arg_text(a, 2));
            if (a.size() > 3) keys(a[3], [&](const std::string& k) {
                if (input::get_button(c, d, k) == gt3r::s8(f)) input::set_button(c, d, k, input::ButtonAction::None);
            });
        });
        return Value{};
    });
    // (0x190D38, 0x191648)
    option.method("setAnalogConfig", [edit](Vm&, const Value&, Args a) {
        edit(a, [&](input::DeviceConfig& c, input::Device d) { input::set_axis(c, d, arg_text(a, 2), input::axis_action(arg_text(a, 3))); });
        return Value{};
    });
    option.method("setButtonConfig", [edit](Vm&, const Value&, Args a) {
        edit(a, [&](input::DeviceConfig& c, input::Device d) { input::set_button(c, d, arg_text(a, 2), input::button_action(arg_text(a, 3))); });
        return Value{};
    });
    // (0x190EA8, 0x1917B8)
    option.method("getAnalogConfig", [read](Vm&, const Value&, Args a) {
        return read(a, [&](const input::DeviceConfig& c, input::Device d) { return text(std::string(input::axis_action_name(input::AxisAction(input::get_axis(c, d, arg_text(a, 2)))))); });
    });
    option.method("getButtonConfig", [read](Vm&, const Value&, Args a) {
        return read(a, [&](const input::DeviceConfig& c, input::Device d) { return text(std::string(input::button_action_name(input::ButtonAction(input::get_button(c, d, arg_text(a, 2)))))); });
    });
    // (0x191078, 0x191988: the keys with the action; an action without a name has none: the scripts' "" for "no such kind")
    option.method("searchAnalogConfig", [read, list_of](Vm&, const Value&, Args a) {
        return read(a, [&](const input::DeviceConfig& c, input::Device d) {
            const input::AxisAction f = input::axis_action(arg_text(a, 2));
            return list_of(f == input::AxisAction::None ? std::vector<std::string>{} : input::keys_with(c, d, f));
        });
    });
    option.method("searchButtonConfig", [read, list_of](Vm&, const Value&, Args a) {
        return read(a, [&](const input::DeviceConfig& c, input::Device d) {
            const input::ButtonAction f = input::button_action(arg_text(a, 2));
            return list_of(f == input::ButtonAction::None ? std::vector<std::string>{} : input::keys_with(c, d, f));
        });
    });
    // A device's preset (0x18FCC8 / 0x18FD00) and its feedbacks (0x18FD50 / 0x18FD88).
    option.method("getDeviceConfig", [s](Vm&, const Value&, Args a) {
        const Target t = named_target(a, 0);
        return t.device < 0 || t.port < 0 || t.port > 1 ? Value{} : number(port_of(*s, t.port).device[size_t(t.device)].preset);
    });
    option.method("setDeviceConfig", [s](Vm&, const Value&, Args a) {
        const Target t = named_target(a, 1);
        int preset = 1;
        for (const Value& v : a)
            if (v.kind != Kind::String) {
                preset = int(v.as_float());
                break;
            }
        if (t.device < 0 || t.port < 0 || t.port > 1) return Value{};
        input::PortConfig c = port_of(*s, t.port);
        input::set_preset(c.device[size_t(t.device)], input::Device(t.device), preset);
        set_port(*s, t.port, c);
        return Value{};
    });
    const auto feedback_name = [](Args a) {
        int strings = 0;
        for (const Value& v : a)
            if (v.kind == Kind::String && strings++ == 1) return v.as_string();
        return std::string("ON");
    };
    option.method("getDeviceFeedback", [s, feedback_name](Vm&, const Value&, Args a) {
        const Target t = named_target(a, 0);
        return t.device < 0 || t.port < 0 || t.port > 1 ? number(0)
                                                        : number(input::get_feedback(port_of(*s, t.port).device[size_t(t.device)], input::Device(t.device), feedback_name(a)));
    });
    option.method("setDeviceFeedback", [s, feedback_name](Vm&, const Value&, Args a) {
        const Target t = named_target(a, 1);
        if (t.device < 0 || t.port < 0 || t.port > 1 || a.empty()) return Value{};
        input::PortConfig c = port_of(*s, t.port);
        input::set_feedback(c.device[size_t(t.device)], input::Device(t.device), feedback_name(a), int(a[0].as_float()));
        set_port(*s, t.port, c);
        return Value{};
    });
}

// The options' reset (0x4363A0): every device's keys as its first preset has them; the vibration and the wheel's forces stay.
void reset_controllers(GameState& s, const std::array<std::string, 2>& before) {
    for (int port = 0; port < 2; ++port) {
        input::PortConfig c = input::port_from_text(before[size_t(port)]);
        for (size_t d = 0; d < input::kDeviceCount; ++d) input::set_preset(c.device[d], input::Device(d), 0);
        set_port(s, port, c);
    }
}

} // namespace gt4r::menu
