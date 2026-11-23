#pragma once

// A menu project as the game's files give it: the layout (projects/<name>/<name>.mproject),
// each page's pictures (projects/<name>/<language>/<Page>.gpb) and the text
// (projects/<name>/<language>/rtext.rt2 over rtext/<language>.rt2); docs/formats/menu.md.

#include "formats/tex1.h"
#include "gt4r/menu/image_pack.h"
#include "gt4r/menu/layout.h"
#include "gt4r/menu/text_table.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gt4r::menu {

// A file of the disc by its path; nothing when it is not there.
using FileReader = std::function<std::optional<Bytes>(const std::string& path)>;
// A folder's entries (folders end in "/").
using FolderLister = std::function<std::vector<std::string>(const std::string& folder)>;

// The projects of a disc: "boot", "arcade", "gtmode", ...
std::vector<std::string> list_projects(const FolderLister& list);
// The language folders a project has ("US"; Prologue: "JP", "TW").
std::vector<std::string> project_languages(const FolderLister& list, const std::string& project);

class Project {
public:
    // Throws FormatError when the layout is missing or does not read.
    Project(FileReader read, std::string name, std::string language);

    const std::string& name() const { return name_; }         // "boot"
    const std::string& language() const { return language_; } // "US"
    const Widget& layout() const { return layout_; }          // the "Project" widget
    std::string module_name() const { return layout_.name(); } // "BootProject"
    // Its pages: the root windows.
    const std::vector<Widget>& pages() const { return layout_.children(); }
    const TextTable& text() const { return text_; }

    // A widget's text for a key: the page's own ("BootProject::LanguageRoot"), else the key.
    std::string translate(const std::string& page, const std::string& key) const;
    // A page's picture by the name a widget gives ("image/boot/lang_bg_4.png"); null when the
    // page's pack has none. Decoded once.
    const gt3r::Tex1::Image* image(const std::string& page, const std::string& path);

private:
    struct Pack {
        Bytes file;
        std::optional<ImagePack> pack;
        std::map<std::string, std::unique_ptr<gt3r::Tex1::Image>> images;
    };
    Pack& pack_of(const std::string& page);

    FileReader read_;
    std::string name_, language_;
    Widget layout_;
    TextTable text_;
    std::map<std::string, Pack> packs_;
};

} // namespace gt4r::menu
