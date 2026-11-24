#pragma once

// A car of Gran Turismo 4's race step: the shared engine's mirror of the car record, and beside it what
// Gran Turismo 4's record keeps that the mirror has no member for (each area's own part: rules_*.h).

#include "gt4r/sim/rules_aids.h"
#include "gt4r/sim/rules_body.h"
#include "gt4r/sim/rules_chassis.h"
#include "gt4r/sim/rules_drive.h"
#include "gt4r/sim/rules_input.h"
#include "gt4r/sim/rules_speeds.h"
#include "gt4r/sim/rules_steer.h"
#include "gt4r/sim/rules_tires.h"
#include "gt4r/sim/rules_wear.h"
#include "sim/exact/car_record.h"

namespace gt4r::sim {

struct Extra {
    rules::TireExtra tires;
    rules::ChassisExtra chassis;
    rules::DriveExtra drive;
    rules::AidExtra aids;
    rules::InputExtra input;
    rules::WearExtra wear;
    rules::BodyExtra body;
    rules::SteerExtra steer;
    rules::SpeedExtra speeds;
};

struct Record {
    gt3r::sim::exact::Car car;
    Extra x;
    rules::WheelLayout layout; // (the program's table, by the parameter block's byte +0x30)
};

// Every member of Extra with its place in Gran Turismo 4's record: v(name, index, block, offset, member).
template <class X, class V> void extra_fields(X& x, V&& v) {
    rules::tires_fields(x.tires, v), rules::chassis_fields(x.chassis, v), rules::drive_fields(x.drive, v);
    rules::aids_fields(x.aids, v), rules::input_fields(x.input, v), rules::wear_fields(x.wear, v);
    rules::body_fields(x.body, v);
    rules::steer_fields(x.steer, v);
    rules::speeds_fields(x.speeds, v);
}

} // namespace gt4r::sim
