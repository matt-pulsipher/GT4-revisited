#include "gt4r/sim/ai/ee_math.h"

#include "base/ee_math.h"

namespace gt4r::sim::ai {

using gt3r::ee::F;

F atan2(F y, F x) {
    const F zero;
    if (x == zero && y == zero) return zero;
    int flags = 0;
    if (y < zero) y = -y, flags |= 4;
    if (x < zero) x = -x, flags |= 2;
    F r;
    if (y < x) r = gt3r::ee::atan(y / x);
    else flags |= 1, r = gt3r::ee::atan(x / y);
    if (flags & 1) r = F::raw(0x3FC90FDA) - r;
    if (flags & 2) r = F::raw(0x40490FDA) - r;
    if (flags & 4) r = -r;
    return r;
}

} // namespace gt4r::sim::ai
