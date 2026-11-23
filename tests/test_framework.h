#pragma once

// A small test runner: TEST(name) { CHECK(...); }. Tests that need game data read the discs
// named by GT4R_DISC (paths separated by ';') and skip without it.

#include "gt4r/disc/disc.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace gt4r::test {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};
std::vector<TestCase>& registry();
int& failures();
// The discs of GT4R_DISC that open (opened once); empty without the variable.
const std::vector<std::unique_ptr<Disc>>& discs();

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};
struct Skip {
    const char* reason;
};

} // namespace gt4r::test

#define TEST(name)                                                      \
    static void test_##name();                                          \
    static ::gt4r::test::Registrar test_reg_##name(#name, test_##name); \
    static void test_##name()

#define CHECK(cond)                                                   \
    do {                                                              \
        if (!(cond)) {                                                \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++::gt4r::test::failures();                               \
        }                                                             \
    } while (0)

#define SKIP(reason) throw ::gt4r::test::Skip{reason}
