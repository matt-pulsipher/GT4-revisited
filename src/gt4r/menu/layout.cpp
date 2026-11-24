#include "gt4r/menu/layout.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace gt4r::menu {

namespace {

constexpr int kMaxDepth = 64;
constexpr size_t kMaxItems = 1u << 20;

struct Token {
    enum Kind { End, Word, String, Open, Close, ListOpen, ListClose } kind = End;
    std::string text;
};

struct Lexer {
    std::string_view s;
    size_t at = 0;
    Token peeked;
    bool has_peek = false;

    Token read() {
        while (at < s.size() && (s[at] == ' ' || s[at] == '\t' || s[at] == '\n' || s[at] == '\r')) ++at;
        if (at >= s.size()) return {};
        const char c = s[at];
        if (c == '{') return ++at, Token{Token::Open, "{"};
        if (c == '}') return ++at, Token{Token::Close, "}"};
        if (c == '[') return ++at, Token{Token::ListOpen, "["};
        if (c == ']') return ++at, Token{Token::ListClose, "]"};
        Token t;
        if (c == '"') {
            t.kind = Token::String;
            for (++at; at < s.size() && s[at] != '"'; ++at) {
                if (s[at] == '\\' && at + 1 < s.size()) {
                    const char e = s[++at];
                    t.text += e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e;
                } else t.text += s[at];
            }
            if (at >= s.size()) throw FormatError("layout: a string does not end");
            ++at;
            return t;
        }
        t.kind = Token::Word;
        while (at < s.size() && !std::strchr(" \t\r\n{}[]\"", s[at])) t.text += s[at++];
        return t;
    }
    const Token& peek() {
        if (!has_peek) peeked = read(), has_peek = true;
        return peeked;
    }
    Token next() {
        if (has_peek) return has_peek = false, std::move(peeked);
        return read();
    }
    void expect(Token::Kind k, const char* what) {
        if (next().kind != k) throw FormatError(std::string("layout: expected ") + what);
    }
};

bool number_like(const std::string& w) { return !w.empty() && (std::isdigit(u8(w[0])) || w[0] == '-' || w[0] == '+' || w[0] == '.'); }

Widget read_widget(Lexer& lx, std::string type, int depth);

// After "<type> {": a value's items up to "}", or (a word that is no number, then a word or
// "[") a widget's properties.
void read_body(Lexer& lx, const std::string& type, Property* value, Widget* widget, int depth);

Property read_property(Lexer& lx, std::string name, int depth) {
    Property p;
    p.name = std::move(name);
    if (lx.peek().kind == Token::ListOpen) { // name [n] { items }
        lx.next();
        const Token n = lx.next();
        lx.expect(Token::ListClose, "]");
        lx.expect(Token::Open, "{");
        p.is_list = true;
        while (lx.peek().kind != Token::Close) {
            const Token type = lx.next();
            if (type.kind != Token::Word) throw FormatError("layout: expected a list item");
            lx.expect(Token::Open, "{");
            Property value;
            Widget widget;
            read_body(lx, type.text, &value, &widget, depth + 1);
            if (!widget.type.empty()) p.widgets.push_back(std::move(widget));
            else p.items.push_back(std::move(value));
            if (p.items.size() + p.widgets.size() > kMaxItems) throw FormatError("layout: too many items");
        }
        lx.next();
        if (size_t(std::strtoul(n.text.c_str(), nullptr, 10)) != p.items.size() + p.widgets.size())
            throw FormatError("layout: a list's count does not match its items");
        return p;
    }
    const Token type = lx.next();
    if (type.kind != Token::Word) throw FormatError("layout: expected a property's type");
    lx.expect(Token::Open, "{");
    Widget widget;
    read_body(lx, type.text, &p, &widget, depth + 1);
    if (!widget.type.empty()) p.widgets.push_back(std::move(widget)), p.is_list = true; // a widget as a value
    return p;
}

void read_body(Lexer& lx, const std::string& type, Property* value, Widget* widget, int depth) {
    if (depth > kMaxDepth) throw FormatError("layout: nested too deep");
    const Token& first = lx.peek();
    const bool is_value = first.kind == Token::Close || first.kind == Token::String || (first.kind == Token::Word && number_like(first.text));
    if (is_value) {
        value->type = type;
        for (;;) {
            Token t = lx.next();
            if (t.kind == Token::Close) break;
            if (t.kind != Token::Word && t.kind != Token::String) throw FormatError("layout: a value does not end");
            value->values.push_back(std::move(t.text));
            if (value->values.size() > kMaxItems) throw FormatError("layout: too many items");
        }
        return;
    }
    *widget = read_widget(lx, type, depth);
}

// The older files' conditions (Prologue's): `cond_active`, `cond_focus`, `cond_prelight`,
// `cond_insensitive`, each 0 (either), 2 (the state's bit off) or 3 (on), become the
// `visible_condition` / `visible_compare` pair over the bits 1, 2, 4, 8. ❓ Read from the files'
// use (a license sign's grey face 2 / its colored face 3 on `cond_active`), not from the program.
void add_visible_condition(Widget& w) {
    if (w.find("visible_condition") || w.find("visible_compare")) return;
    static constexpr std::pair<const char*, int> kBits[] = {{"cond_active", 1}, {"cond_focus", 2}, {"cond_prelight", 4}, {"cond_insensitive", 8}};
    int condition = 0, compare = 0;
    for (const auto& [name, bit] : kBits) {
        const Property* p = w.find(name);
        if (!p || p->values.empty()) continue;
        const long v = std::strtol(p->values[0].c_str(), nullptr, 10);
        if (v & 2) compare |= bit, condition |= (v & 1) ? bit : 0;
    }
    if (!compare) return;
    auto digit = [](std::string name, int value) {
        Property p;
        p.name = std::move(name), p.type = "digit", p.values = {std::to_string(value)};
        return p;
    };
    w.properties.push_back(digit("visible_condition", condition));
    w.properties.push_back(digit("visible_compare", compare));
}

// After "<type> {": properties up to "}".
Widget read_widget(Lexer& lx, std::string type, int depth) {
    Widget w;
    w.type = std::move(type);
    for (;;) {
        Token t = lx.next();
        if (t.kind == Token::Close) break;
        if (t.kind != Token::Word) throw FormatError("layout: expected a property in " + w.type);
        w.properties.push_back(read_property(lx, std::move(t.text), depth));
    }
    add_visible_condition(w);
    return w;
}

float to_float(const std::string& s) { return std::strtof(s.c_str(), nullptr); }
u8 to_byte(const std::string& s) { return u8(std::min(std::max(std::strtol(s.c_str(), nullptr, 10), 0L), 255L)); }

Rgba to_rgba(const Property& p) {
    Rgba c;
    if (p.values.size() >= 4) c = {to_byte(p.values[0]), to_byte(p.values[1]), to_byte(p.values[2]), to_byte(p.values[3])};
    return c;
}

// The older files (Prologue's: conditions as `cond_*`) have no `clip`: the select bar of a
// scroll box shows only what lies inside it (the title's one item of eight, the arcade's first
// page's Start alone, a course list's five pictures), while the box's arrows and name around it
// stay. ❓ Judged from the original's pages, not read from the program.
void clip_select_bars(Widget& w, bool in_scroll_box) {
    if (in_scroll_box && w.type == "SelectBar" && !w.find("clip")) {
        Property p;
        p.name = "clip", p.type = "digit", p.values = {"1"};
        w.properties.push_back(std::move(p));
    }
    for (Property& p : w.properties)
        for (Widget& c : p.widgets) clip_select_bars(c, w.type == "ScrollBox");
}

} // namespace

Widget parse_layout(std::string_view text) {
    Lexer lx;
    lx.s = text;
    const Token type = lx.next();
    if (type.kind != Token::Word) throw FormatError("layout: no widget");
    lx.expect(Token::Open, "{");
    Widget w = read_widget(lx, type.text, 0);
    if (lx.next().kind != Token::End) throw FormatError("layout: text after the widget");
    if (text.find("cond_active") != std::string_view::npos && text.find("visible_condition") == std::string_view::npos) clip_select_bars(w, false);
    return w;
}

const Property* Widget::find(std::string_view n) const {
    for (const Property& p : properties)
        if (p.name == n) return &p;
    return nullptr;
}

std::string Widget::text(std::string_view property, std::string_view fallback) const {
    const Property* p = find(property);
    return p && !p->values.empty() ? p->values[0] : std::string(fallback);
}

float Widget::number(std::string_view property, float fallback) const {
    const Property* p = find(property);
    return p && !p->values.empty() ? to_float(p->values[0]) : fallback;
}

std::optional<Rect> Widget::rect(std::string_view property) const {
    const Property* p = find(property);
    if (!p || p->values.size() < 4) return std::nullopt;
    return Rect{to_float(p->values[0]), to_float(p->values[1]), to_float(p->values[2]), to_float(p->values[3])};
}

std::optional<Rgba> Widget::color(std::string_view property) const {
    const Property* p = find(property);
    if (!p || p->values.size() < 4) return std::nullopt;
    return to_rgba(*p);
}

std::vector<Rgba> Widget::colors(std::string_view property) const {
    std::vector<Rgba> v;
    if (const Property* p = find(property))
        for (const Property& item : p->items) v.push_back(to_rgba(item));
    return v;
}

const std::vector<Widget>& Widget::children() const {
    static const std::vector<Widget> none;
    const Property* p = find("children");
    return p ? p->widgets : none;
}

const Widget* Widget::child(std::string_view path) const {
    const Widget* at = this;
    while (at && !path.empty()) {
        const size_t end = std::min(path.find_first_of("/:"), path.size());
        const std::string_view part = path.substr(0, end);
        path.remove_prefix(std::min(path.find_first_not_of("/:", end), path.size()));
        if (part.empty()) continue;
        const Widget* found = nullptr;
        for (const Widget& c : at->children())
            if (c.name() == part) {
                found = &c;
                break;
            }
        at = found;
    }
    return at;
}

size_t Widget::count() const {
    size_t n = 1;
    for (const Property& p : properties)
        for (const Widget& w : p.widgets) n += w.count();
    return n;
}

} // namespace gt4r::menu
