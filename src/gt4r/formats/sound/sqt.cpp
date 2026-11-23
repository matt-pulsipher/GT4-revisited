#include "gt4r/formats/sound/sqt.h"

#include <algorithm>

namespace gt4r::sound {

SqtBank SqtBank::parse(ByteSpan file) {
    if (!gt3r::has_magic(file, "Ssqt") || file.size() < 12) throw FormatError("Ssqt: wrong magic");
    const size_t count = size_t(rd32(file, 4)) + 1;
    if (count > (file.size() - 8) / 4) throw FormatError("Ssqt: bad sequence count");
    SqtBank bank;
    bank.data = std::make_shared<const Bytes>(file.begin(), file.end());
    for (size_t i = 0; i < count; ++i) {
        const u32 off = rd32(file, 8 + i * 4);
        if (off > file.size() || file.size() - off < 9 || !gt3r::has_magic(file.subspan(off), "Sssq"))
            throw FormatError("Ssqt: a sequence is not Sssq");
        bank.sequences.push_back({rd32(file, off + 4), off + 8});
    }
    return bank;
}

u32 read_sequence_number(ByteSpan d, size_t& pos) {
    u32 v = 0;
    for (;;) {
        if (pos >= d.size()) throw FormatError("sequence: a number runs past the end");
        const u8 b = d[pos++];
        v = v << 7 | (b & 0x7F);
        if (!(b & 0x80)) return v;
    }
}

std::vector<SqtEvent> decode_sequence(const SqtBank& bank, size_t index) {
    const ByteSpan d = *bank.data;
    size_t pos = bank.sequences.at(index).events;
    const auto byte = [&]() -> u8 {
        if (pos >= d.size()) throw FormatError("Sssq: events run past the end");
        return d[pos++];
    };
    std::vector<SqtEvent> out;
    u8 status = 0;
    u32 tick = 0;
    for (;;) {
        if (pos >= d.size()) throw FormatError("Sssq: no end event");
        if (d[pos] & 0x80) status = d[pos++];
        SqtEvent e;
        e.tick = tick;
        e.channel = status & 0x0F;
        switch (status >> 4) {
        case 0x8:
            e.kind = SqtEvent::Kind::NoteOff;
            e.a = byte();
            break;
        case 0x9:
            e.a = byte();
            e.b = byte();
            e.kind = e.b ? SqtEvent::Kind::NoteOn : SqtEvent::Kind::NoteOff;
            break;
        case 0xB:
            e.kind = SqtEvent::Kind::Control;
            e.a = byte();
            e.b = byte();
            break;
        case 0xC:
            e.kind = SqtEvent::Kind::Program;
            e.a = byte();
            break;
        case 0xE:
            e.kind = SqtEvent::Kind::Bend;
            e.a = byte();
            break;
        case 0xF:
            if (status == 0xFF) {
                const u8 type = byte(), length = byte();
                if (type == 0x2F) {
                    e.kind = SqtEvent::Kind::End;
                } else if (type == 0x51) {
                    e.kind = SqtEvent::Kind::Tempo;
                    for (int i = 0; i < 3; ++i) e.tempo = e.tempo << 8 | byte();
                } else {
                    e.kind = SqtEvent::Kind::Meta;
                    e.a = type;
                    for (u8 i = 0; i < length; ++i) byte();
                }
            }
            break;
        default: // no event: the stream's opening time, or a status the game skips
            break;
        }
        if (e.kind != SqtEvent::Kind::None) out.push_back(e);
        if (e.kind == SqtEvent::Kind::End) return out;
        tick += read_sequence_number(d, pos);
    }
}

SqtSummary summarize_sequence(const SqtBank& bank, size_t index) {
    const std::vector<SqtEvent> events = decode_sequence(bank, index);
    const u32 resolution = std::max<u32>(bank.sequences.at(index).resolution, 1);
    SqtSummary s;
    u32 tempo = 500000, last_tick = 0;
    double seconds = 0, at_loop_start = 0, at_loop_end = -1;
    for (const SqtEvent& e : events) {
        seconds += double(e.tick - last_tick) * tempo / (1e6 * resolution);
        last_tick = e.tick;
        switch (e.kind) {
        case SqtEvent::Kind::Tempo: tempo = e.tempo; break;
        case SqtEvent::Kind::NoteOn:
            if (!s.notes) s.first_tempo = tempo;
            ++s.notes;
            s.channels |= 1u << e.channel;
            break;
        case SqtEvent::Kind::Program:
            if (std::find(s.programs.begin(), s.programs.end(), e.a) == s.programs.end()) s.programs.push_back(e.a);
            break;
        case SqtEvent::Kind::Control:
            if (e.a == kCtlLoop && e.b == kLoopStart) {
                s.loop_start = e.tick;
                at_loop_start = seconds;
            } else if (e.a == kCtlLoop && e.b == kLoopEnd && s.loop_end < 0) {
                s.loop_end = e.tick;
                at_loop_end = seconds;
            }
            break;
        default: break;
        }
    }
    s.ticks = last_tick;
    if (s.loop_start >= 0 && at_loop_end >= 0) {
        s.intro_seconds = at_loop_start;
        s.loop_seconds = at_loop_end - at_loop_start;
    }
    std::sort(s.programs.begin(), s.programs.end());
    return s;
}

} // namespace gt4r::sound
