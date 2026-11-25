#include "gt4r/race/aspec_points.h"

namespace gt4r::race {

using gt3r::ee::F;

int aspec_points(std::span<const F> ratings, size_t car, bool mission, bool loose) {
    const size_t n = ratings.size();
    if (n <= 1 || car >= n) return 0;
    if (mission) return 250;
    const F zero, one = F::raw(0x3F800000), ten = F::raw(0x41200000), top = F::raw(0x43480000);
    F sum;
    for (size_t i = 0; i < n; ++i)
        if (i != car) sum = sum + ratings[i];
    const F over = (sum / gt3r::ee::int_to_f(s32(n - 1)) + (loose ? F::raw(0x3D8F5C28) : F::raw(0x3CF5C28F))) - ratings[car];
    F points;
    if (over < zero) {
        const F least = F::raw(0x3DCCCCCC);
        points = over * ten + one;
        if (points < least) points = least;
    } else {
        points = over * top;
        if (points < one) points = one;
    }
    points = points * ten;
    if (top < points) points = top;
    return gt3r::ee::to_int((points + F::raw(0x3F000000)).bits) & 0xFF;
}

} // namespace gt4r::race
