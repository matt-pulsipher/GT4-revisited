#pragma once

// Where a portable.ini keeps the user data beside the program (app.cpp's user_data_dir).

#include <string>

namespace gt4r {

// The folder holding a macOS app bundle whose resources are at `base`, SDL's base path for a bundle
// (".../gt4r.app/Contents/Resources/", where an added file breaks the bundle's signature): there a portable.ini
// sits beside the app. Empty when `base` is not a bundle's.
inline std::string app_folder(const std::string& base) {
    static const std::string inside = ".app/Contents/Resources/";
    if (base.size() <= inside.size() || base.compare(base.size() - inside.size(), inside.size(), inside) != 0) return {};
    const size_t slash = base.find_last_of('/', base.size() - inside.size()); // the one before "<name>.app"
    return slash == std::string::npos ? std::string() : base.substr(0, slash + 1);
}

} // namespace gt4r
