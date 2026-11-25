#include "gt4r/app/page.h"

#include "gt4r/app/app.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>

namespace gt4r {

using gt3r::UiRect;
using Pointer = gt3r::PointerInput::Action;

int ListScroll::keep(int selected, int count, int shown) {
    if (selected < first) first = selected;
    if (selected >= first + shown) first = selected - shown + 1;
    first = std::clamp(first, 0, std::max(0, count - shown));
    return first;
}

void ListScroll::turn(float wheel, int& selected, int count, int shown) {
    if (wheel == 0 || count <= 0) return;
    const int by = wheel > 0 ? -std::max(1, int(std::lround(wheel))) : std::max(1, int(std::lround(-wheel)));
    first = std::clamp(first + by, 0, std::max(0, count - shown));
    selected = std::clamp(selected, first, std::min(count, first + shown) - 1);
}

void RowPointer::rows(int list, int first, int count, float x, float y, float w, float h) {
    if (count > 0) areas_.push_back({{x, y, w, h * float(count)}, list, first, count});
}

void RowPointer::place(int list, int row, const UiRect& r) { areas_.push_back({r, list, row, 1}); }

std::vector<RowPointer::Hit> RowPointer::read(App& app) const {
    std::vector<Hit> out;
    for (const Pointer& a : app.pointer()) {
        if (a.kind != Pointer::Kind::Hover && a.kind != Pointer::Kind::Click) continue;
        for (auto it = areas_.rbegin(); it != areas_.rend(); ++it) {
            if (!it->r.contains(a.x, a.y)) continue;
            const int row = std::clamp(int((a.y - it->r.y) / (it->r.h / float(it->count))), 0, it->count - 1);
            out.push_back({it->list, it->first + row, a.kind == Pointer::Kind::Click});
            break;
        }
    }
    return out;
}

float RowPointer::wheel(App& app, int list) const {
    float turns = 0;
    for (const Pointer& a : app.pointer())
        if (a.kind == Pointer::Kind::Wheel)
            for (const Area& area : areas_)
                if (area.list == list && area.r.contains(a.x, a.y)) {
                    turns += a.wheel;
                    break;
                }
    return turns;
}

bool RowPointer::pick(App& app, int list, int& selected, int count) const {
    bool open = false;
    if (const float turns = wheel(app, list); turns != 0) selected -= turns > 0 ? std::max(1, int(std::lround(turns))) : std::min(-1, int(std::lround(turns)));
    for (const Hit& h : read(app))
        if (h.click && h.list == list) open = h.row == selected, selected = h.row;
    selected = std::clamp(selected, 0, std::max(0, count - 1));
    return open;
}

int RowPointer::over(App& app, int list) const {
    float x = 0, y = 0;
    if (!app.mouse_at(x, y)) return -1;
    for (const Area& a : areas_)
        if (a.list == list && a.r.contains(x, y)) return a.first + std::clamp(int((y - a.r.y) / (a.r.h / float(a.count))), 0, a.count - 1);
    return -1;
}

// --- rows ---

PageRow& PageRows::heading(std::string label) {
    PageRow r;
    r.kind = PageRow::Kind::Heading, r.label = std::move(label);
    return rows.emplace_back(std::move(r));
}

PageRow& PageRows::info(std::string label, std::string value) {
    PageRow r;
    r.label = std::move(label), r.value = std::move(value);
    return rows.emplace_back(std::move(r));
}

PageRow& PageRows::action(std::string label, std::string value, std::string hint, std::function<void()> press) {
    PageRow r;
    r.kind = PageRow::Kind::Action, r.label = std::move(label), r.value = std::move(value);
    r.press = std::move(press), r.press_hint = std::move(hint);
    return rows.emplace_back(std::move(r));
}

PageRow& PageRows::choice(std::string label, const std::vector<std::string>& names, size_t now, std::function<void(size_t)> set) {
    PageRow r;
    r.kind = PageRow::Kind::Choice, r.label = std::move(label);
    const size_t n = names.size();
    now = n ? std::min(now, n - 1) : 0;
    r.value = n ? names[now] : std::string();
    r.change = [n, now, set = std::move(set)](int d) {
        if (n > 1) set(size_t((long(now) + d + long(n)) % long(n)));
    };
    return rows.emplace_back(std::move(r));
}

PageRow& PageRows::pick(std::string label, const std::vector<std::pair<std::string, std::string>>& options, const std::string& now, std::function<void(const std::string&)> set) {
    std::vector<std::string> names, values;
    size_t at = 0;
    for (const auto& [value, name] : options) {
        if (value == now) at = names.size();
        names.push_back(name), values.push_back(value);
    }
    return choice(std::move(label), names, at, [values, set = std::move(set)](size_t i) { set(values[i]); });
}

PageRow& PageRows::slider(std::string label, float now, float lo, float hi, float step, const std::function<std::string(float)>& text, std::function<void(float)> set) {
    PageRow r;
    r.kind = PageRow::Kind::Slider, r.label = std::move(label), r.value = text(now);
    r.fraction = hi > lo ? std::clamp((now - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    const auto stepped = [lo, hi, step](float v) { return std::clamp(std::round(v / step) * step, lo, hi); };
    r.change = [now, step, stepped, set](int d) { set(stepped(now + float(d) * step)); };
    r.slide = [now, lo, hi, stepped, set](float f) {
        if (const float v = stepped(lo + (hi - lo) * std::clamp(f, 0.0f, 1.0f)); v != now) set(v);
    };
    return rows.emplace_back(std::move(r));
}

void PageRows::move(int d) {
    for (int i = selected + d; i >= 0 && i < int(rows.size()); i += d)
        if (rows[size_t(i)].selectable()) {
            selected = i;
            break;
        }
}

PageRows::Done PageRows::update(App& app) {
    Done done;
    const MenuInput& m = app.menu();
    const int count = int(rows.size());
    selected = std::min(selected, count - 1);
    if (selected < 0 || !rows[size_t(selected)].selectable()) {
        // The first row that takes anything, at or after the one that was selected.
        const int was = std::max(0, selected);
        selected = -1;
        for (int i = was; i < count && selected < 0; ++i)
            if (rows[size_t(i)].selectable()) selected = i;
        for (int i = std::min(was, count) - 1; i >= 0 && selected < 0; --i)
            if (rows[size_t(i)].selectable()) selected = i;
    }
    if (selected < 0) {
        done.above = m.vertical() < 0;
        return done;
    }
    const auto press = [&](PageRow& r) {
        if (r.press) r.press();
        else if (r.change) r.change(1);
        else return;
        done.changed = true;
    };
    // (A press rebuilds nothing until the next frame: one thing a frame.)
    if (const int v = m.vertical()) {
        const int before = selected;
        move(v);
        if (selected == before && v < 0) done.above = true;
        scroll.keep(selected, count, shown_);
        // (the headings over the first row that can be selected show too)
        bool first = true;
        for (int i = 0; i < selected; ++i) first = first && !rows[size_t(i)].selectable();
        if (first) scroll.first = 0;
    } else if (PageRow& r = rows[size_t(selected)]; r.change && m.horizontal()) {
        r.change(m.horizontal()), done.changed = true;
    } else if (m.pressed(gt3r::PadButton::Cross)) {
        press(r);
    } else if (m.pressed(gt3r::PadButton::Square) && r.other) {
        r.other(), done.changed = true;
    }
    if (done.changed || done.above) return done;
    for (const Pointer& a : app.pointer()) {
        if (a.kind == Pointer::Kind::Wheel) {
            if (!area_.contains(a.x, a.y)) continue;
            const int most = std::max(0, count - shown_);
            scroll.first = std::clamp(scroll.first - int(std::lround(a.wheel > 0 ? std::max(1.0f, a.wheel) : std::min(-1.0f, a.wheel))), 0, most);
            continue;
        }
        if (a.kind == Pointer::Kind::DragEnd) dragging_ = -1;
        if (a.kind == Pointer::Kind::Drag) {
            if (dragging_ < 0)
                for (const Place& p : places_)
                    if (rows[size_t(p.row)].slide && p.track.w > 0 && UiRect{p.track.x - 8, p.r.y, p.track.w + 16, p.r.h}.contains(a.sx, a.sy)) dragging_ = selected = p.row;
            if (dragging_ >= 0 && dragging_ < count)
                for (const Place& p : places_)
                    if (p.row == dragging_ && rows[size_t(p.row)].slide) rows[size_t(p.row)].slide((a.x - p.track.x) / p.track.w), done.changed = true;
            if (done.changed) return done;
            continue;
        }
        if (a.kind != Pointer::Kind::Hover && a.kind != Pointer::Kind::Click) continue;
        for (const Place& p : places_) {
            if (p.row >= count || !p.r.contains(a.x, a.y) || !rows[size_t(p.row)].selectable()) continue;
            selected = p.row;
            if (a.kind == Pointer::Kind::Hover) break;
            PageRow& r = rows[size_t(p.row)];
            if (r.slide) {
                if (UiRect{p.track.x - 8, p.r.y, p.track.w + 16, p.r.h}.contains(a.x, a.y)) r.slide((a.x - p.track.x) / p.track.w), done.changed = true;
            } else if (r.change && p.back.contains(a.x, a.y)) r.change(-1), done.changed = true;
            else if (r.change && p.forward.contains(a.x, a.y)) r.change(1), done.changed = true;
            else press(r);
            return done;
        }
    }
    return done;
}

void PageRows::render(Ui& ui, const UiRect& area, bool focused) {
    area_ = area;
    shown_ = std::max(1, int(area.h / kRow));
    places_.clear();
    const int count = int(rows.size());
    scroll.first = std::clamp(scroll.first, 0, std::max(0, count - shown_));
    for (int i = scroll.first; i < std::min(count, scroll.first + shown_); ++i) {
        const PageRow& r = rows[size_t(i)];
        const float y = area.y + float(i - scroll.first) * kRow;
        if (r.kind == PageRow::Kind::Heading) {
            ui.text(area.x + 2, y + 4, r.label, colors::kAccent);
            continue;
        }
        const bool on = focused && i == selected;
        const UiRect box{area.x, y, area.w - 6, kRow - 2};
        if (r.kind != PageRow::Kind::Info) ui.fill_round(box.x, box.y, box.w, box.h, 3, on ? colors::kHighlight : colors::kPanel);
        const Color c = on || (r.kind == PageRow::Kind::Info && !r.value.empty()) ? colors::kText : colors::kDim;
        Place p{box, {}, {}, {}, i};
        float right = box.right() - 8;
        if (r.kind == PageRow::Kind::Choice && r.enabled && on) {
            ui.text_right(right, y + 3, ">", colors::kAccent);
            p.forward = {right - 12, y, box.right() - right + 12, box.h};
            right -= 10;
        }
        const float label_w = Ui::text_width(r.label);
        float room = std::max(40.0f, right - box.x - 10 - label_w - 24);
        if (r.kind == PageRow::Kind::Slider) {
            const float w = 120;
            p.track = {right - w, y, w, box.h};
            ui.slider(p.track.x, y + box.h / 2, w, r.fraction, on, r.enabled);
            right -= w + 12, room = std::max(40.0f, room - w - 12);
        }
        const std::string value = Ui::elide(r.value, room);
        ui.text_right(right, y + 3, value, r.bad ? colors::kBad : r.enabled ? c : colors::kDim);
        float left = right - Ui::text_width(value);
        if (r.kind == PageRow::Kind::Choice && r.enabled && on) {
            ui.text_right(left - 5, y + 3, "<", colors::kAccent);
            p.back = {left - 18, y, 18, box.h};
            left -= 16;
        }
        ui.text(box.x + 10, y + 3, r.label, r.enabled ? c : colors::kDim);
        if (!r.tag.empty()) ui.tag(std::max(box.x + 20 + label_w, left - 14 - Ui::text_width(r.tag) - 6), y + 3, r.tag, colors::kAccent);
        places_.push_back(p);
    }
    ui.scrollbar(area, float(count) * kRow, float(scroll.first) * kRow);
}

std::string PageRows::hint() const {
    if (selected < 0 || selected >= int(rows.size())) return {};
    const PageRow& r = rows[size_t(selected)];
    std::string out;
    if (r.change) out = r.slide ? "{Left/Right}: adjust" : "{Left/Right}: change";
    if (r.press && !r.press_hint.empty()) out += (out.empty() ? "" : "  ") + ("{Cross}: " + r.press_hint);
    if (r.other && !r.other_hint.empty()) out += (out.empty() ? "" : "  ") + ("{Square}: " + r.other_hint);
    return out;
}

// --- a question ---

void PageQuestion::ask(std::string title, std::vector<std::string> lines, std::string yes, std::string no, std::function<void()> on_yes) {
    open_ = true, on_yes_button_ = false;
    title_ = std::move(title), lines_ = std::move(lines), yes_ = std::move(yes), no_ = std::move(no);
    yes_action_ = std::move(on_yes);
    yes_r_ = no_r_ = {};
}

void PageQuestion::update(App& app) {
    const MenuInput& m = app.menu();
    int answer = 0; // 1 yes, -1 no
    if (m.horizontal() || m.vertical()) on_yes_button_ = !on_yes_button_;
    if (m.pressed(gt3r::PadButton::Cross)) answer = on_yes_button_ ? 1 : -1;
    if (m.pressed(gt3r::PadButton::Triangle) || m.pressed(gt3r::PadButton::Circle)) answer = -1;
    for (const Pointer& a : app.pointer()) {
        if (a.kind == Pointer::Kind::Hover) on_yes_button_ = yes_r_.contains(a.x, a.y) || (on_yes_button_ && !no_r_.contains(a.x, a.y));
        if (a.kind == Pointer::Kind::Click) answer = yes_r_.contains(a.x, a.y) ? 1 : no_r_.contains(a.x, a.y) ? -1 : answer;
    }
    if (!answer) return;
    open_ = false;
    const std::function<void()> act = std::move(yes_action_);
    if (answer > 0 && act) act();
}

void PageQuestion::render(Ui& ui) {
    const float w = 380, h = 70 + 12 * float(lines_.size()), x = (ui.width() - w) / 2, y = (ui.height() - h) / 2;
    ui.fill(0, 0, ui.width(), ui.height(), Color{0, 0, 0, 170});
    ui.fill_round(x, y, w, h, 6, colors::kPanel);
    ui.frame_round(x, y, w, h, 6, 1, colors::kOutline);
    ui.text(x + 16, y + 14, Ui::clip(title_, w - 32), colors::kAccent);
    for (size_t i = 0; i < lines_.size(); ++i) ui.text(x + 16, y + 32 + 12 * float(i), Ui::clip(lines_[i], w - 32), colors::kText);
    yes_r_ = {x + w - 16 - 110, y + h - 28, 110, 18}, no_r_ = {x + w - 16 - 110 - 8 - 110, y + h - 28, 110, 18};
    ui.button(no_r_, no_, !on_yes_button_, false);
    ui.button(yes_r_, yes_, on_yes_button_, false);
}

// --- the system's windows ---

struct FileWindow::Slot {
    std::mutex lock;
    bool answered = false;
    std::string path, error;
};

namespace {

// (The window may answer from another thread and after the page is gone: the callback holds its own reference.)
template <class Slot> void SDLCALL on_answer(void* user, const char* const* files, int) {
    std::unique_ptr<std::shared_ptr<Slot>> held(static_cast<std::shared_ptr<Slot>*>(user));
    const std::lock_guard<std::mutex> lock((*held)->lock);
    if (!files) (*held)->error = SDL_GetError();
    else if (files[0]) (*held)->path = files[0];
    (*held)->answered = true;
}

} // namespace

// A scripted run (and GT4R_DIALOG) answers by itself: no window opens over whoever is at the machine.
bool FileWindow::scripted(App& app) {
    slot_ = std::make_shared<Slot>();
    const char* path = std::getenv("GT4R_DIALOG");
    if (!path && !app.scripted()) return false;
    slot_->path = path ? path : "", slot_->answered = true;
    return true;
}

void FileWindow::open_file(App& app, const SDL_DialogFileFilter* filters, int count) {
    if (slot_ || scripted(app)) return;
    SDL_ShowOpenFileDialog(&on_answer<Slot>, new std::shared_ptr<Slot>(slot_), app.window(), filters, count, nullptr, false);
}

void FileWindow::open_folder(App& app) {
    if (slot_ || scripted(app)) return;
    SDL_ShowOpenFolderDialog(&on_answer<Slot>, new std::shared_ptr<Slot>(slot_), app.window(), nullptr, false);
}

void FileWindow::save_file(App& app, const SDL_DialogFileFilter* filters, int count, const std::string& name) {
    if (slot_ || scripted(app)) return;
    SDL_ShowSaveFileDialog(&on_answer<Slot>, new std::shared_ptr<Slot>(slot_), app.window(), filters, count, name.c_str());
}

bool FileWindow::take(std::string& path, std::string& error) {
    if (!slot_) return false;
    {
        const std::lock_guard<std::mutex> lock(slot_->lock);
        if (!slot_->answered) return false;
        path = slot_->path, error = slot_->error;
    }
    slot_.reset();
    return true;
}

} // namespace gt4r
