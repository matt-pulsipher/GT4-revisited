#include "gt4r/sim/record_image.h"

#include "gt4r/sim/car_layout.h"
#include "sim/exact/car_fields.h"

#include <cstring>
#include <type_traits>

namespace gt4r::sim {

void load_car_record(Record& r, std::span<const gt3r::u8> car) {
    auto put = [&](const layout::Place& place, auto& m) {
        using T = std::remove_cvref_t<decltype(m)>;
        constexpr size_t size = std::is_same_v<T, gt3r::ee::F> ? 4 : sizeof(T);
        if (!place || place.block != layout::Block::Car || place.offset + size > car.size()) return;
        std::memcpy(&m, car.data() + place.offset, size);
    };
    gt3r::sim::exact::fields(r.car, [&](const char*, int, u32 off, auto& m) { put(layout::place(off), m); });
    extra_fields(r.x, [&](const char*, int, layout::Block b, u32 off, auto& m) { put({b, off}, m); });
}

} // namespace gt4r::sim
