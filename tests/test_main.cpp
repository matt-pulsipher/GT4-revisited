#include "test_framework.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace gt4r::test {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

int& failures() {
    static int n = 0;
    return n;
}

const std::vector<std::unique_ptr<Disc>>& discs() {
    static std::vector<std::unique_ptr<Disc>> list;
    static bool opened = false;
    if (opened) return list;
    opened = true;
    const char* env = std::getenv("GT4R_DISC");
    std::string all = env ? env : "";
    for (size_t at = 0; at <= all.size();) {
        size_t end = all.find(';', at);
        if (end == std::string::npos) end = all.size();
        const std::string path = all.substr(at, end - at);
        at = end + 1;
        if (path.empty()) continue;
        try {
            list.push_back(Disc::open(path));
        } catch (const std::exception& e) {
            std::printf("  FAIL GT4R_DISC %s: %s\n", path.c_str(), e.what());
            ++failures();
        }
    }
    return list;
}

} // namespace gt4r::test

int main(int argc, char** argv) {
    using namespace gt4r::test;
    // The tests' folders (temp_directory_path() / "gt4r-...") under a folder of this run's own, so
    // two runs at once (two worktrees' suites) never share one; taken away at the end.
    namespace fs = std::filesystem;
    std::error_code ec;
#if defined(_WIN32)
    const fs::path own = fs::temp_directory_path(ec) / ("gt4r-tests-" + std::to_string(_getpid()));
#else
    const fs::path own = fs::temp_directory_path(ec) / ("gt4r-tests-" + std::to_string(getpid()));
#endif
    const bool made = !ec && fs::create_directories(own, ec) && !ec;
    if (made) {
#if defined(_WIN32)
        _putenv_s("TMP", own.string().c_str()), _putenv_s("TEMP", own.string().c_str());
#else
        setenv("TMPDIR", own.c_str(), 1);
#endif
    }
    int ran = 0, skipped = 0;
    for (const TestCase& t : registry()) {
        if (argc > 1 && !std::strstr(t.name, argv[1])) continue;
        const int before = failures();
        try {
            t.fn();
            ++ran;
            std::printf("%s %s\n", failures() == before ? "ok  " : "FAIL", t.name);
        } catch (const Skip& s) {
            ++skipped;
            std::printf("skip %s (%s)\n", t.name, s.reason);
        } catch (const std::exception& e) {
            ++failures();
            std::printf("FAIL %s: %s\n", t.name, e.what());
        }
    }
    std::printf("%d run, %d skipped, %d failed checks\n", ran, skipped, failures());
    if (made) fs::remove_all(own, ec);
    return failures() ? 1 : 0;
}
