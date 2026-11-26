#include "gt4r/net/car_service.h"

#include <algorithm>
#include <cstring>

namespace gt4r::net {

namespace {

constexpr u8 kVersion = 1;
constexpr u8 kMagic[4] = {'G', '4', 'S', 'V'}; // (the block's last 4 bytes; a u16 length of the section before them)
constexpr size_t kTrailer = 6;
constexpr s32 kStep = 50;
constexpr u16 kAbsolute = 0xFFFF;

bool valid_kind(u8 k) { return k >= ServiceEvent::Hold && k <= ServiceEvent::Flags; }

void write_value(gt3r::net::Writer& w, const ServiceEvent& e) {
    switch (e.kind) {
    case ServiceEvent::Hold: w.u16_(u16(e.value)), w.u8_(u8(e.value >> 16)); break;
    case ServiceEvent::Tires: w.u8_(u8(e.value)), w.u8_(u8(e.value >> 8)), w.u8_(u8(e.value >> 16)); break;
    case ServiceEvent::Grips:
    case ServiceEvent::NewTires:
    case ServiceEvent::Flags: w.u8_(u8(e.value)); break;
    case ServiceEvent::Driver: w.u16_(u16(e.value)); break;
    default: w.u32_(e.value); break;
    }
}

u32 read_value(gt3r::net::Reader& r, ServiceEvent::Kind k) {
    switch (k) {
    case ServiceEvent::Hold: {
        const u32 count = r.u16_();
        return count | (u32(r.u8_() & 1) << 16);
    }
    case ServiceEvent::Tires: {
        const u32 f = r.u8_(), b = r.u8_(), a = r.u8_();
        return f | (b << 8) | ((a & 3) << 16);
    }
    case ServiceEvent::Grips: return r.u8_() & 3;
    case ServiceEvent::NewTires: return r.u8_() & 0xF;
    case ServiceEvent::Flags: return r.u8_();
    case ServiceEvent::Driver: return r.u16_();
    default: return r.u32_();
    }
}

Bytes encode_section(const CarService& s, size_t first) {
    gt3r::net::Writer w;
    w.u8_(kVersion);
    w.u16_(s.round);
    const size_t n = std::min<size_t>(s.events.size() - first, 255);
    w.u8_(u8(n));
    s32 at = 0;
    for (size_t k = first; k < first + n; ++k) {
        const ServiceEvent& e = s.events[k];
        w.u8_(e.kind);
        const s64 d = s64(e.clock) - at;
        if (k > first && d >= 0 && d % kStep == 0 && d / kStep < kAbsolute) w.u16_(u16(d / kStep));
        else w.u16_(kAbsolute), w.u32_(u32(e.clock));
        at = e.clock;
        write_value(w, e);
    }
    w.u16_(u16(w.data.size()));
    w.bytes(ByteSpan(kMagic, 4));
    return w.data;
}

// The section's bytes within a block, with its trailer; empty for none.
ByteSpan section_span(ByteSpan block) {
    if (block.size() < kTrailer || std::memcmp(block.data() + block.size() - 4, kMagic, 4) != 0) return {};
    const size_t len = size_t(block[block.size() - 6]) | (size_t(block[block.size() - 5]) << 8);
    if (len + kTrailer > block.size() || len < 4) return {};
    return block.subspan(block.size() - kTrailer - len, len + kTrailer);
}

} // namespace

u16 hold_at(const std::vector<ServiceEvent>& events, s64 clock) {
    const ServiceEvent* last = nullptr;
    for (const ServiceEvent& e : events)
        if (e.kind == ServiceEvent::Hold && e.clock <= clock && (!last || e.moment() >= last->moment())) last = &e;
    if (!last) return 0;
    const s64 count = s64(last->value & 0xFFFF);
    if (last->value & 0x10000) return u16(count);
    return u16(std::max<s64>(0, count - (clock - last->clock) / kStep));
}

ByteSpan record_of(ByteSpan block) {
    const ByteSpan s = section_span(block);
    return block.first(block.size() - s.size());
}

Bytes with_service(ByteSpan record, const CarService& service, size_t limit) {
    const ByteSpan rec = record_of(record);
    for (size_t first = service.events.size() > 255 ? service.events.size() - 255 : 0;; ++first) {
        const Bytes s = encode_section(service, std::min(first, service.events.size()));
        if (rec.size() + s.size() <= limit || first >= service.events.size()) {
            Bytes out(rec.begin(), rec.end());
            if (out.size() + s.size() <= limit) out.insert(out.end(), s.begin(), s.end());
            return out;
        }
    }
}

std::optional<CarService> service_of(ByteSpan block) {
    const ByteSpan s = section_span(block);
    if (s.empty()) return std::nullopt;
    gt3r::net::Reader r(s.first(s.size() - kTrailer));
    if (r.u8_() != kVersion) return std::nullopt;
    CarService out;
    out.round = r.u16_();
    const size_t n = r.u8_();
    s32 at = 0;
    for (size_t k = 0; k < n && r.ok(); ++k) {
        const u8 kind = r.u8_();
        if (!valid_kind(kind)) return std::nullopt;
        ServiceEvent e;
        e.kind = ServiceEvent::Kind(kind);
        const u16 d = r.u16_();
        e.clock = d == kAbsolute ? s32(r.u32_()) : s32(s64(at) + s64(d) * kStep);
        at = e.clock;
        e.value = read_value(r, e.kind);
        out.events.push_back(e);
    }
    if (!r.ok() || !r.at_end()) return std::nullopt;
    return out;
}

void ServiceLog::push(const ServiceEvent& e) {
    // (in the order of the car's run: an event after a step goes before those of the next step's start)
    auto at = events_.end();
    while (at != events_.begin() && std::prev(at)->moment() > e.moment()) --at;
    events_.insert(at, e);
    changed_ = true;
}

void ServiceLog::hold(s32 clock, u16 count) {
    if (hold_at(holds_, clock) == count) {
        last_count_ = count;
        return;
    }
    const bool standing = count == last_count_ && count != 0;
    ServiceEvent e{clock, ServiceEvent::Hold, u32(count) | (standing ? 0x10000u : 0u)};
    holds_.push_back(e);
    if (holds_.size() > 8) holds_.erase(holds_.begin());
    last_count_ = count;
    push(e);
}

void ServiceLog::add(const ServiceEvent& e) {
    if (e.kind == ServiceEvent::Hold) return hold(e.clock, u16(e.value));
    if (e.kind == ServiceEvent::Fuel) {
        fuel_ = e, fuel_this_step_ = true;
        return;
    }
    if (e.kind == ServiceEvent::Flags) {
        if (u8(e.value) == last_flags_) return;
        last_flags_ = u8(e.value);
    }
    if (e.kind == ServiceEvent::Driver) {
        if (last_driver_ == e.value) return;
        last_driver_ = e.value;
    }
    push(e);
}

void ServiceLog::end_step(s32 clock) {
    (void)clock;
    if (fuel_ && !fuel_this_step_) push(*fuel_), fuel_.reset();
    fuel_this_step_ = false;
}

std::vector<ServiceEvent> ServiceLog::recent(s64 clock) const {
    std::vector<ServiceEvent> out;
    for (const ServiceEvent& e : events_)
        if (s64(e.clock) >= clock - kKeep) out.push_back(e);
    return out;
}

} // namespace gt4r::net
