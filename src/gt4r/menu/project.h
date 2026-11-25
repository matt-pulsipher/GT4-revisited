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
    // Another text for a key the table has.
    void reword(const std::string& page, const std::string& key, const std::string& text) { text_.reword(page, key, text); }

    // A widget's text for a key: the page's own ("BootProject::LanguageRoot"), else the key.
    std::string translate(const std::string& page, const std::string& key) const;
    // A page's picture by the name a widget gives ("image/boot/lang_bg_4.png"); null when the
    // page's pack has none. Decoded once. `plain`: with its alpha as it is stored, else as a picture face draws it, which in
    // Prologue is another scale (face_alpha).
    const gt3r::Tex1::Image* image(const std::string& page, const std::string& path, bool plain = false);
    // Prologue's picture faces draw in a vertex alpha of 63 (0x2B4BA8: half the color's alpha, halved again by 0x1D7240): their
    // pictures keep alpha as 0 .. 255, where the others' 128 is whole.
    void set_face_alpha(bool on) { face_alpha_ = on; }
    static Bytes face_alpha(const gt3r::Tex1& set, Bytes data);
    // Any other file of a page's pack by its name (a model face's "image/gtmode/map/panel_x.mdl"),
    // unpacked; nothing when the page's packs have none.
    std::optional<Bytes> entry(const std::string& page, const std::string& path);
    // Whether the pack of `page`, one loaded since, or one of the packs the scripts loaded has the entry.
    bool has_entry(const std::string& page, const std::string& path);
    // More pictures a script loads for a page while it shows (`context.loadGpb(folder, name)`:
    // <folder>/<language>/<name>.gpb): looked in after the page's own. False when the file is
    // not there.
    bool load_pack(const std::string& folder, const std::string& name);
    void unload_pack(const std::string& name);

private:
    struct Pack {
        Bytes file;
        std::optional<ImagePack> pack;
        std::map<std::string, std::unique_ptr<gt3r::Tex1::Image>> images;
    };
    Pack& pack_of(const std::string& page);

    bool face_alpha_ = false;
    FileReader read_;
    std::string name_, language_;
    Widget layout_;
    TextTable text_;
    std::map<std::string, Pack> packs_;
    std::map<std::string, Pack> extra_; // by the name a script gave
};

} // namespace gt4r::menu
