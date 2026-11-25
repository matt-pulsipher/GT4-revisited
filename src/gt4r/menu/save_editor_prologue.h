#pragma once

// What the save editor changes in a saved game of Gran Turismo 4 Prologue: the name its results pages offer and the license
// tests' medals with their rankings (docs/formats/prologue.md "The saved game", "The license tests"). It keeps no money, garage
// or calendar; its course times' and options' meanings are not read and stay as they are.

#include "gt4r/menu/runtime.h"

#include <string>
#include <vector>

namespace gt4r::menu {

class PrologueSaveEditor {
public:
    PrologueSaveEditor(FileReader read, RuntimeConfig config);
    ~PrologueSaveEditor();
    void start(const GameState& loaded);
    GameState& state() { return state_; }

    // The three letters a result is entered under.
    static constexpr size_t kNameLetters = 3;
    void set_name(const std::string& v);
    struct Test {
        std::string label, name; // "l0p0001", "Test 1"
        int unit = -1;
        int medal = 0;    // 0 none, 1 bronze, 2 silver, 3 gold
        u32 best = kNoTime;
        u32 limit[4] = {}; // the medals' times (0: none; a bronze without one is for any finish)
    };
    const std::vector<Test>& tests() const { return tests_; }
    // The medal set; the ranking loses the times that earn more and takes the medal's own time when none left earns it (as a
    // run's result is kept: recordResult). None: never passed, the ranking empty.
    void set_medal(size_t test, int medal);

private:
    std::unique_ptr<Runtime> rt_;
    GameState state_;
    bool started_ = false;
    std::vector<Test> tests_;
    void read_tests();
    u8* unit(int i);
};

} // namespace gt4r::menu
