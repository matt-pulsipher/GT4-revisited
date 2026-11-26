#pragma once

// The app's name where a player or a log reader sees it: the window, the system's app name, message boxes, the log, and
// what the engine says for the app (its base/app_identity.h: the log's file names and first line, the text a host sends a
// player whose game is too old). gt4r.log and gt4r.prev.log in the data folder.
//
// An engine without base/app_identity.h (before the shared engine's hook) still names the log gt3r.log with a "GT3
// Revisited" first line; everything else here is ours either way.

#include "gt4r_version.h"

#include <string>

#if __has_include("base/app_identity.h")
#include "base/app_identity.h"
#include "base/log_file.h"
#define GT4R_ENGINE_APP_IDENTITY 1
#else
#define GT4R_ENGINE_APP_IDENTITY 0
#endif

namespace gt4r {

inline constexpr const char* kAppName = "GT4 Revisited";
inline constexpr const char* kAppId = "org.gt4revisited.gt4r";

// Once, first thing at start (before the log file opens).
inline void name_the_engine() {
#if GT4R_ENGINE_APP_IDENTITY
    gt3r::set_app_identity({kAppName, GT4R_VERSION, "gt4r"});
#endif
}

// The log's file name in the data folder: this session's, or the one before.
inline std::string log_file_name(bool previous) {
#if GT4R_ENGINE_APP_IDENTITY
    return gt3r::log_file::file_name(previous);
#else
    return previous ? "gt3r.prev.log" : "gt3r.log";
#endif
}

} // namespace gt4r
