// The program's opponent picker (game_opponents.h).

#include "gt4r/menu/game_opponents.h"

#include "gt4r/menu/game_internal.h"

#include <algorithm>

namespace gt4r::menu::opponents {

namespace {

using gt3r::ee::int_to_f;
using gt3r::ee::to_int;

// 0x130660: the higher evaluation first.
int compare(const Candidate& a, const Candidate& b) {
    if (a.score < b.score) return 1;
    if (b.score < a.score) return -1;
    return 0;
}

// The C library's quicksort on list[base, base + n).
void quicksort(std::vector<Candidate>& v, int base, int n) {
    auto cmp = [&](int a, int b) { return compare(v[size_t(a)], v[size_t(b)]); };
    auto swap = [&](int a, int b) { std::swap(v[size_t(a)], v[size_t(b)]); };
    auto vecswap = [&](int a, int b, int k) {
        for (int i = 0; i < k; ++i) swap(a + i, b + i);
    };
    auto med3 = [&](int a, int b, int c) {
        return cmp(a, b) < 0 ? (cmp(b, c) < 0 ? b : (cmp(a, c) < 0 ? c : a)) : (cmp(b, c) > 0 ? b : (cmp(a, c) < 0 ? a : c));
    };
    auto insertion = [&](int from, int k) {
        for (int pm = from + 1; pm < from + k; ++pm)
            for (int pl = pm; pl > from && cmp(pl - 1, pl) > 0; --pl) swap(pl, pl - 1);
    };
    for (;;) {
        if (n < 7) {
            insertion(base, n);
            return;
        }
        int pm = base + n / 2;
        if (n > 7) {
            int pl = base, pn = base + n - 1;
            if (n > 40) {
                const int d = n / 8;
                pl = med3(pl, pl + d, pl + 2 * d);
                pm = med3(pm - d, pm, pm + d);
                pn = med3(pn - 2 * d, pn - d, pn);
            }
            pm = med3(pl, pm, pn);
        }
        swap(base, pm);
        int pa = base + 1, pb = pa, pc = base + n - 1, pd = pc;
        bool swapped = false;
        for (;;) {
            int r;
            while (pb <= pc && (r = cmp(pb, base)) <= 0) {
                if (r == 0) swapped = true, swap(pa, pb), ++pa;
                ++pb;
            }
            while (pb <= pc && (r = cmp(pc, base)) >= 0) {
                if (r == 0) swapped = true, swap(pc, pd), --pd;
                --pc;
            }
            if (pb > pc) break;
            swap(pb, pc);
            swapped = true;
            ++pb, --pc;
        }
        if (!swapped) {
            insertion(base, n);
            return;
        }
        const int pn = base + n;
        int r = std::min(pa - base, pb - pa);
        vecswap(base, pb - r, r);
        r = std::min(pd - pc, pn - pd - 1);
        vecswap(pb, pn - r, r);
        if ((r = pb - pa) > 1) quicksort(v, base, r);
        if ((r = pd - pc) > 1) {
            base = pn - r, n = r;
            continue;
        }
        return;
    }
}

// 0x579398: a draw of the program's generator (0x578598) times x.
F draw_times(ProgramRandom& random, F x) {
    const float u = random.next01();
    return F(u) * x;
}

} // namespace

int match_category(int category_byte) {
    const int c = category_byte % 100;
    return c == 3 ? 0 : c;
}

bool category_allowed(int player, int c) { return c == player || (player == 2 && c == 1); }

void sort(std::vector<Candidate>& list) { quicksort(list, 0, int(list.size())); }

int search(const std::vector<Candidate>& list, int lo, int hi, F x) {
    for (;;) {
        const int mid = (lo + hi) / 2;
        if (mid == lo) return mid;
        if (!(list[size_t(mid)].score < x)) lo = mid;
        else hi = mid;
    }
}

std::vector<int> draw(ProgramRandom& random, int lo, int hi, int count, int exclude) {
    std::vector<int> out(size_t(std::max(count, 0)), -1);
    const int size = hi - lo;
    int left = size, picked = 0;
    while (picked < count) {
        if (left <= 0) break;
        if (exclude >= 0 && left < 2) break;
        const int place = to_int(draw_times(random, int_to_f(size))) + lo;
        if (place == exclude) continue;
        if (std::find(out.begin(), out.begin() + picked, place) != out.begin() + picked) continue;
        out[size_t(picked++)] = place;
        --left;
    }
    return out;
}

void dirt_places(std::vector<int>& places, const std::vector<Candidate>& list, int exclude, const DirtRace& dirt_race) {
    for (int& place : places) {
        int at = place;
        for (;;) {
            // (outside the list the program reads memory that names no car: its lookup fails and it stops)
            if (at < 0 || size_t(at) >= list.size()) return;
            const int r = dirt_race(list[size_t(at)].label);
            if (r < 0) return;
            if (r > 0 && at != exclude) break;
            ++at;
        }
        place = at;
    }
}

std::vector<Pick> pick(const PickInput& in, ProgramRandom& random) {
    const int count = in.count < 6 ? in.count : 5;
    const int player = match_category(in.player_category);
    // The list's cars of an allowed category (a car the database lacks keeps the category last read).
    std::vector<Candidate> list;
    int last = in.player_category;
    for (const std::string& label : in.list) {
        int c = 0;
        if (in.category && in.category(label, c)) last = c;
        if (!category_allowed(player, match_category(last))) continue;
        list.push_back({label, in.score ? in.score(label) : F::raw(0)});
    }
    const F target = in.player_score + in.offset;
    sort(list);
    const int n = int(list.size());
    int lo = search(list, 0, n, target + in.range);
    int hi = search(list, 0, n, target - in.range);
    // The window widened to count + 1 cars: toward lower evaluations first, then higher.
    const int need = count + 1;
    int k = hi - lo;
    while (k < need && hi < n - 1) ++k, ++hi;
    while (k < need && lo > 0) ++k, --lo;
    int exclude = -1;
    for (int i = lo; i < hi; ++i)
        if (list[size_t(i)].label == in.player_label) {
            exclude = i;
            break;
        }
    std::vector<int> places = draw(random, lo, hi, count, exclude);
    if (in.dirt && in.dirt_race) dirt_places(places, list, exclude, in.dirt_race);
    // A paint for each place in turn, from its car's paints (❓ a place of -1 draws over no paints).
    std::vector<int> paints(places.size(), 0);
    for (size_t i = 0; i < places.size(); ++i) {
        const int p = places[i];
        const int colors = p >= 0 && p < n && in.paints ? in.paints(list[size_t(p)].label) : 0;
        paints[i] = to_int(draw_times(random, int_to_f(colors)));
    }
    // The places in falling order (an insertion sort of the places alone: the paints stay in their turns).
    for (size_t i = 1; i < places.size(); ++i) {
        const int v = places[i];
        size_t j = i;
        while (j > 0 && places[j - 1] < v) places[j] = places[j - 1], --j;
        places[j] = v;
    }
    std::vector<Pick> out;
    for (size_t i = 0; i < places.size(); ++i) {
        if (places[i] < 0) continue;
        out.push_back({list[size_t(places[i])].label, paints[i]});
    }
    return out;
}

} // namespace gt4r::menu::opponents
