#pragma once

// The app's name where a player or a log reader sees it: the window, the system's app name, message boxes, the log, and
// what the engine says for the app (its base/app_identity.h: the log's file names and first line, the text a host sends a
// player whose game is too old). gt4r.log and gt4r.prev.log in the data folder.

#include "gt4r_version.h"

#include <string>

#include "base/app_identity.h"
#include "base/log_file.h"

namespace gt4r {

inline constexpr const char* kAppName = "GT4 Revisited";
inline constexpr const char* kAppId = "org.gt4revisited.gt4r";

// Once, first thing at start (before the log file opens).
inline void name_the_engine() { gt3r::set_app_identity({kAppName, GT4R_VERSION, "gt4r"}); }

// The log's file name in the data folder: this session's, or the one before.
inline std::string log_file_name(bool previous) { return gt3r::log_file::file_name(previous); }

} // namespace gt4r
