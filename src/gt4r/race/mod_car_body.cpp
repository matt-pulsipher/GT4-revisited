#include "gt4r/race/mod_car_body.h"

#include "gt4r/formats/db/car_parts.h"
#include "gt4r/formats/db/mod_cars.h"
#include "gt4r/race/car_spec.h"

#include "base/file.h"
#include "base/log.h"

#include <cmath>
#include <cstdio>

namespace gt4r::race {

namespace {

// A car's length, wheelbase and height (its stock CHASSIS row) and width (its paints' box), mm.
std::optional<std::array<float, 4>> dimensions(db::SpecDb& db, const std::string& label) {
    const std::optional<db::CarParts> parts = db::car_parts(db, label);
    if (!parts) return std::nullopt;
    const std::optional<db::Chassis> ch = db::Chassis::read(db::part_row(db, parts->part(db::PartTable::Chassis)));
    const std::optional<std::array<float, 4>> box = car_box(db, label);
    if (!ch || !box || !ch->length_mm || !ch->wheelbase_mm || !ch->height_mm || (*box)[1] <= 0) return std::nullopt;
    return std::array<float, 4>{float(ch->length_mm), float(ch->wheelbase_mm), (*box)[1] * 1000.0f, float(ch->height_mm)};
}

} // namespace

std::string ModCarBody::key() const {
    char b[160];
    std::snprintf(b, sizeof b, "%s|%.0f %.0f %.0f %.0f|%.3f %.3f %.3f|%.3f", glb_path.c_str(), fit ? fit->to_length : 0.0, fit ? fit->to_wheelbase : 0.0,
                  fit ? fit->to_width : 0.0, fit ? fit->to_height : 0.0, paint ? (*paint)[0] : -1.0, paint ? (*paint)[1] : -1.0, paint ? (*paint)[2] : -1.0,
                  double(lower));
    return b;
}

std::optional<ModCarBody> mod_car_body(db::SpecDb& db, const std::string& label, bool menu, int time_of_day, int paint) {
    const db::ModCarDef* d = db.mod_cars().def(label);
    if (!d) return std::nullopt;
    ModCarBody out;
    if (const std::string path = d->model_path(menu, time_of_day); !path.empty()) {
        if (std::optional<Bytes> f = gt3r::read_host_file(path)) {
            out.glb = std::make_shared<const Bytes>(std::move(*f));
            out.glb_path = path;
            out.lower = d->model_lower_mm * 0.001f;
            if (!d->paints.empty()) {
                const std::array<u8, 3>& c = d->paints[size_t(std::max(paint, 0)) % d->paints.size()].second;
                out.paint = std::array<float, 3>{c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f};
            }
        } else {
            LOG_WARN("Mod car %s: its body %s is not read: the body car's is drawn", label.c_str(), path.c_str());
        }
    }
    // The borrowed body stretched to the car's dimensions (a body of its own is made to them).
    if (!out.glb && d->fit_body) {
        const std::string body = d->body.empty() ? d->donor : d->body;
        const std::optional<std::array<float, 4>> from = dimensions(db, body), to = dimensions(db, label);
        bool differs = false;
        if (from && to)
            for (size_t i = 0; i < 4; ++i) differs = differs || std::fabs((*to)[i] / (*from)[i] - 1.0f) > 0.002f;
        if (differs) {
            gt3r::BodyFit f;
            f.from_length = (*from)[0], f.from_wheelbase = (*from)[1], f.from_width = (*from)[2], f.from_height = (*from)[3];
            f.to_length = (*to)[0], f.to_wheelbase = (*to)[1], f.to_width = (*to)[2], f.to_height = (*to)[3];
            out.fit = f;
        }
    }
    if (!out.glb && !out.fit) return std::nullopt;
    return out;
}

} // namespace gt4r::race
