// Gran Turismo 4's own routines of the race step: what every area shares (rules.h).

#include "gt4r/sim/rules.h"

#include "base/ee_math.h"

namespace gt4r::sim::rules {

gt3r::sim::Behaviour behaviour() { return {gt3r::sim::Program::ConceptGenevaAsia, 60}; }

void body_axes(F pitch, F roll, F heading, ex::Vec3& fwd, ex::Vec3& lat, ex::Vec3& up) {
    const F sp = gt3r::ee::sin(pitch), cp = gt3r::ee::cos(pitch);
    const F sr = gt3r::ee::sin(roll), cr = gt3r::ee::cos(roll);
    const F sh = gt3r::ee::sin(heading), ch = gt3r::ee::cos(heading);
    const F chsp = ch * sp, shsp = sh * sp;
    const F shsr = sh * sr, chsr = ch * sr;
    const F chspcr = chsp * cr, shspcr = shsp * cr;
    const F chcr = ch * cr, shspsr = shsp * sr;
    const F shcr = sh * cr, chspsr = chsp * sr;
    lat = {chcr - shspsr, shcr + chspsr, (-cp) * sr};
    fwd = {(-sh) * cp, ch * cp, sp};
    up = {chsr + shspcr, shsr - chspcr, cp * cr};
}

} // namespace gt4r::sim::rules
