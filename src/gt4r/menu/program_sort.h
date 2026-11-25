#pragma once

// The program's sort of a script array (0x2F03C8: the introsort of its C++ library, 0x5EC8D0 and 0x5ECB28): quick sort by the
// median of three down to runs of 16, then an insertion sort over all. Not stable: which of two equal items comes first
// follows its swaps (a garage of more than 16 cars by a column they share stands nearly turned round).

#include <algorithm>
#include <utility>
#include <vector>

namespace gt4r::menu {

template <class T, class Less> void program_sort(std::vector<T>& v, Less less) {
    const long n = long(v.size());
    if (n < 2) return;
    int depth = 0;
    for (long k = n; k != 1; k >>= 1) ++depth;
    depth *= 2;
    // (Out of its depth it goes over to a heap sort: ❓ whose ties are the host library's here.)
    auto loop = [&](auto&& self, long first, long last, int limit) -> void {
        while (last - first > 16) {
            if (limit == 0) {
                std::partial_sort(v.begin() + first, v.begin() + last, v.begin() + last, less);
                return;
            }
            --limit;
            const T &a = v[size_t(first)], &b = v[size_t(first + (last - first) / 2)], &c = v[size_t(last - 1)];
            const T pivot = less(a, b) ? less(b, c) ? b : less(a, c) ? c : a : less(a, c) ? a : less(b, c) ? c : b;
            long i = first, j = last;
            for (;;) {
                while (less(v[size_t(i)], pivot)) ++i;
                --j;
                while (less(pivot, v[size_t(j)])) --j;
                if (!(i < j)) break;
                std::swap(v[size_t(i)], v[size_t(j)]);
                ++i;
            }
            self(self, i, last, limit);
            last = i;
        }
    };
    loop(loop, 0, n, depth);
    auto unguarded = [&](long last) {
        T value = v[size_t(last)];
        long next = last - 1;
        while (less(value, v[size_t(next)])) v[size_t(last)] = v[size_t(next)], last = next, --next;
        v[size_t(last)] = std::move(value);
    };
    const long head = std::min<long>(n, 16);
    for (long i = 1; i < head; ++i) {
        if (less(v[size_t(i)], v[0])) {
            T value = v[size_t(i)];
            for (long k = i; k > 0; --k) v[size_t(k)] = v[size_t(k - 1)];
            v[0] = std::move(value);
        } else unguarded(i);
    }
    for (long i = head; i < n; ++i) unguarded(i);
}

} // namespace gt4r::menu
