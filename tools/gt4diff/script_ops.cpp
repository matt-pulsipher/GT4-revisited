// gt4r_diff: the script interpreter's operators on values of two kinds (docs/formats/script.md "Values of two kinds"): the
// program's own value classes' operator slots run in the interpreter on values made here (an int, a float, a text), against
// our Vm::binary; and each class's integer and float (the tests JUMP_ZERO, && and || make) against value_int / value_float.
// Routines are named by address (US v1.01); nothing of the program is kept.

#include "orig.h"

#include "gt4r/script/vm.h"
#include "gt4r/sim/program.h"
#include "base/ee_float.h"

#include <bit>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace gt4r::diff {

namespace {

using script::Kind;
using script::Value;

// The value classes' vtables (each slot 8 bytes: delta, index, function) and the operators' slots (the table 0x619CA8
// the loader reads an operator's name into).
constexpr u32 kIntVt = 0x674188, kFloatVt = 0x673D60, kStringVt = 0x675268;
constexpr u32 kAlloc = 0x326750, kFree = 0x326798; // the program's heap (size, ...) and its release
constexpr u32 kSprintf = 0x57DA20, kToDouble = 0x57FB50; // sprintf (buffer, format, ...); a float made a double
struct OpSlot {
    const char* op;
    int slot;
};
constexpr OpSlot kOps[] = {{"+", 22},  {"-", 23},  {"*", 24}, {"/", 25}, {"%", 26}, {"==", 28}, {"!=", 29}, {">", 30}, {">=", 31},
                           {"<", 32},  {"<=", 33}, {"&", 43}, {"|", 44}, {"^", 45}, {"<<", 46}, {">>", 47}};

u32 slot_fn(Ee& ee, u32 vt, int slot) { return ee.r32(vt + 8 * u32(slot) + 4); }

// A value as the program's object: the vtable at +4, the value at +0x10 (a text's characters with their length 0x10
// before them).
u32 make(Ee& ee, const Value& v) {
    const u32 o = ee.alloc(0x20, 16);
    std::memset(ee.host_ptr(o, 0x20), 0, 0x20);
    if (v.kind == Kind::Int) ee.w32(o + 4, kIntVt), ee.w32(o + 0x10, u32(v.i));
    else if (v.kind == Kind::Float) ee.w32(o + 4, kFloatVt), ee.w32(o + 0x10, std::bit_cast<u32>(v.f));
    else {
        const std::string& s = v.as_string();
        const u32 rep = ee.alloc(0x10 + u32(s.size()) + 1, 16);
        std::memset(ee.host_ptr(rep, 0x10 + u32(s.size()) + 1), 0, 0x10 + s.size() + 1);
        ee.w32(rep, u32(s.size())), ee.w32(rep + 4, u32(s.size()));
        if (!s.empty()) std::memcpy(ee.host_ptr(rep + 0x10, u32(s.size())), s.data(), s.size());
        ee.w32(o + 4, kStringVt), ee.w32(o + 0x10, rep + 0x10);
    }
    return o;
}

std::string show(const Value& v) {
    char b[64];
    if (v.kind == Kind::Int) std::snprintf(b, sizeof b, "%d", v.i);
    else if (v.kind == Kind::Float) std::snprintf(b, sizeof b, "%.9g (%08X)", double(v.f), std::bit_cast<u32>(v.f));
    else if (v.kind == Kind::String) return "\"" + v.as_string() + "\"";
    else return v.kind == Kind::Nil ? "nil" : "?";
    return b;
}

bool same(const Value& a, const Value& b) {
    if (a.kind != b.kind) return false;
    if (a.kind == Kind::Int) return a.i == b.i;
    if (a.kind == Kind::Float) return std::bit_cast<u32>(a.f) == std::bit_cast<u32>(b.f);
    return a.kind == Kind::String && a.as_string() == b.as_string();
}

} // namespace

bool run_script_ops(Disc& disc) {
    if (!sim::Program::load(disc).known() || sim::Program::load(disc).release() != sim::Release::Us) {
        std::printf("script: needs SCUS-97328 v1.01's program\n");
        return false;
    }
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc, sdb);
    ee.hle(kAlloc, [](Ee& e) { e.set_v0(e.alloc(u32(e.a(0)) + 16, 16)); }, "heap");
    ee.hle(kFree, [](Ee&) {}, "heap release");

    const std::vector<Value> ints = {Value::integer(0), Value::integer(1), Value::integer(-1), Value::integer(7), Value::integer(-7), Value::integer(3400),
                                     Value::integer(33), Value::integer(INT32_MIN), Value::integer(INT32_MAX)};
    const std::vector<Value> floats = {Value::number(0.5f), Value::number(-0.5f), Value::number(2.9f), Value::number(-2.9f), Value::number(0.1f),
                                       Value::number(3.0e9f), Value::number(-3.0e9f), Value::number(1.0e-30f), Value::number(0.0f), Value::number(-0.0f),
                                       Value::number(std::bit_cast<float>(0x7FFFFFFFu))};
    const std::vector<Value> texts = {Value::string("3395"), Value::string("12abc"), Value::string(""), Value::string(" 42"), Value::string("-7"),
                                      Value::string("0x10"), Value::string("1e3"), Value::string("1.75"), Value::string("abc"),
                                      Value::string("99999999999"), Value::string("0.1"), Value::string("3.14159265"), Value::string("1e-40"),
                                      Value::string("123456789.123"), Value::string("inf"), Value::string("nan"), Value::string("  +.5"),
                                      Value::string("5."), Value::string("."), Value::string("-"), Value::string("1e"), Value::string("1e+"),
                                      Value::string("4e38"), Value::string("2.5e-3x"), Value::string("0X1p3"), Value::string("\t-0012")};
    std::vector<Value> all = ints;
    all.insert(all.end(), floats.begin(), floats.end());
    all.insert(all.end(), texts.begin(), texts.end());

    script::Vm vm;
    int runs = 0, equal = 0, shown = 0, unrun = 0;
    // The integer and the float of every value (vtable slots 11 and 12).
    for (const Value& v : all) {
        const u32 o = make(ee, v), vt = ee.r32(o + 4);
        const s32 pi = ee.call(slot_fn(ee, vt, 11), {Arg::ptr(o)}).v0_s32();
        const u32 pf = ee.call(slot_fn(ee, vt, 12), {Arg::ptr(o)}).f0;
        const bool ok = pi == script::value_int(v) && pf == std::bit_cast<u32>(script::value_float(v));
        ++runs, equal += ok;
        if (!ok && shown++ < 4000)
            std::printf("  of %s: program %d / %08X, ours %d / %08X\n", show(v).c_str(), pi, pf, script::value_int(v), std::bit_cast<u32>(script::value_float(v)));
    }
    // Each operator of an int's and a float's on every value.
    for (const std::vector<Value>* lefts : {&ints, &floats, &texts})
        for (const Value& a : *lefts)
            for (const Value& b : all)
                for (const OpSlot& op : kOps) {
                    const u32 vt = a.kind == Kind::Int ? kIntVt : a.kind == Kind::Float ? kFloatVt : kStringVt;
                    const u32 fn = slot_fn(ee, vt, op.slot);
                    if (a.kind == Kind::Float && op.slot >= 43) continue; // a float has none of its own: none here either
                    if (a.kind == Kind::String && (op.slot >= 24 && op.slot <= 27 || op.slot >= 34)) continue; // nor a text
                    if (a.kind == Kind::String && b.kind == Kind::Float) continue; // a float as text: the float texts below
                    const bool divide = op.slot == 25 || op.slot == 26;
                    Value ours;
                    bool ours_threw = false;
                    try {
                        ours = vm.binary(op.op, a, b);
                    } catch (const script::ScriptError&) {
                        ours_threw = true;
                    }
                    const u32 left = make(ee, a), right = make(ee, b), lh = ee.alloc(16), rh = ee.alloc(16), out = ee.alloc(16);
                    ee.w32(lh, left), ee.w32(rh, right), ee.w32(out, 0);
                    bool trapped = false;
                    try {
                        ee.call(fn, {Arg::ptr(left), Arg::ptr(out), Arg::ptr(lh), Arg::integer(1), Arg::ptr(rh)});
                    } catch (const gt3r::diff::EeTrap& t) {
                        trapped = t.kind == gt3r::diff::TrapKind::Break;
                        if (!trapped) {
                            if (unrun++ < 10) std::printf("  %s %s %s: not run here (%s)\n", show(a).c_str(), op.op, show(b).c_str(), t.what());
                            continue;
                        }
                    }
                    Value program;
                    if (!trapped) {
                        const u32 r = ee.r32(out), rvt = r ? ee.r32(r + 4) : 0;
                        if (rvt == kIntVt) program = Value::integer(s32(ee.r32(r + 0x10)));
                        else if (rvt == kFloatVt) program = Value::number(std::bit_cast<float>(ee.r32(r + 0x10)));
                        else if (rvt == kStringVt) {
                            const u32 chars = ee.r32(r + 0x10), n = ee.r32(chars - 0x10);
                            program = Value::string(n ? std::string(reinterpret_cast<const char*>(ee.host_ptr(chars, n)), n) : std::string());
                        }
                    }
                    const bool ok = trapped ? ours_threw && divide : !ours_threw && same(program, ours);
                    ++runs, equal += ok;
                    if (!ok && shown++ < 4000)
                        std::printf("  %s %s %s: program %s, ours %s\n", show(a).c_str(), op.op, show(b).c_str(), trapped ? "traps" : show(program).c_str(),
                                    ours_threw ? "stops" : show(ours).c_str());
                }
    std::printf("script operators: %d of %d equal (%d the interpreter could not run)\n", equal, runs, unrun);
    // The program's sprintf (0x57DA20) of a float made a double (0x57FB50), as hFloat's toString and the scripts' formats
    // print it, against the host's.
    int printed_equal = 0, printed = 0;
    for (const char* format : {"%f", "%.2f", "%.1f", "%g"})
        for (float x : {0.5f, 1.0f, 0.75f, 1.25f, 0.1f, 0.95f, 1.05f, 2.9f, -0.5f, 100.0f, 0.005f, 1234.5678f, 1.0e-7f, 1.4f, 3.2f, 22.0f, 2.8f, 5.6f, 0.3f, 0.7f, 0.35f, 0.25f, 0.125f, 1.5f, 2.5f}) {
            const u32 buf = ee.alloc(128), fmt = ee.alloc_cstr(format);
            const u64 d = ee.call(kToDouble, {Arg::flt(x)}).v0.lo;
            if (std::getenv("SCRIPT_OPS_DOUBLES")) std::printf("  double of %.9g: program %016llX, host %016llX\n", double(x), (unsigned long long)d, (unsigned long long)std::bit_cast<u64>(double(x)));
            ee.call(kSprintf, {Arg::ptr(buf), Arg::ptr(fmt), Arg::integer(s64(d))});
            const std::string program = ee.read_cstr(buf, 128);
            char host[128];
            std::snprintf(host, sizeof host, format, double(x));
            ++printed, printed_equal += program == host;
            if (program != host) std::printf("  \"%s\" of %.9g: program \"%s\", host \"%s\"\n", format, double(x), program.c_str(), host);
        }
    std::printf("float texts: %d of %d as the host prints them\n", printed_equal, printed);
    return equal == runs;
}

} // namespace gt4r::diff
