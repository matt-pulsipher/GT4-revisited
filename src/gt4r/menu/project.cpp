#include "gt4r/menu/project.h"

#include "formats/gs_memory.h"

#include <algorithm>

namespace gt4r::menu {

std::vector<std::string> list_projects(const FolderLister& list) {
    std::vector<std::string> out;
    for (const std::string& e : list("projects/")) {
        if (e.empty() || e.back() != '/') continue;
        const std::string name = e.substr(0, e.size() - 1);
        const std::vector<std::string> inside = list("projects/" + e);
        if (std::find(inside.begin(), inside.end(), name + ".mproject") != inside.end()) out.push_back(name);
    }
    return out;
}

std::vector<std::string> project_languages(const FolderLister& list, const std::string& project) {
    std::vector<std::string> out;
    for (const std::string& e : list("projects/" + project + "/"))
        if (!e.empty() && e.back() == '/') out.push_back(e.substr(0, e.size() - 1));
    return out;
}

Project::Project(FileReader read, std::string name, std::string language)
    : read_(std::move(read)), name_(std::move(name)), language_(std::move(language)) {
    const std::optional<Bytes> layout = read_("projects/" + name_ + "/" + name_ + ".mproject");
    if (!layout) throw FormatError("no layout for project " + name_);
    layout_ = parse_layout(*layout);
    // The common text, then the project's own over it.
    for (const std::string& path : {"rtext/" + language_ + ".rt2", "projects/" + name_ + "/" + language_ + "/rtext.rt2"})
        if (const std::optional<Bytes> file = read_(path); file && TextTable::is_table(*file)) text_.merge(TextTable::parse(*file));
}

std::string Project::translate(const std::string& page, const std::string& key) const {
    if (const std::string* t = text_.find(module_name() + "::" + page, key)) return *t;
    return key;
}

Project::Pack& Project::pack_of(const std::string& page) {
    const auto it = packs_.find(page);
    if (it != packs_.end()) return it->second;
    Pack& p = packs_[page];
    if (std::optional<Bytes> file = read_("projects/" + name_ + "/" + language_ + "/" + page + ".gpb"); file && ImagePack::is_pack(*file)) {
        p.file = std::move(*file);
        p.pack = ImagePack::parse(p.file);
    }
    return p;
}

const gt3r::Tex1::Image* Project::image(const std::string& page, const std::string& path) {
    Pack& p = pack_of(page);
    if (!p.pack) return nullptr;
    if (const auto it = p.images.find(path); it != p.images.end()) return it->second.get();
    std::unique_ptr<gt3r::Tex1::Image>& slot = p.images[path];
    const ImagePack::Entry* e = p.pack->find(path);
    if (!e) return nullptr;
    try {
        const Bytes data = p.pack->read(*e);
        if (!gt3r::Tex1::is_tex1(data)) return nullptr;
        const gt3r::Tex1 set = gt3r::Tex1::parse(data);
        if (set.textures.empty()) return nullptr;
        gt3r::GsMemory gs;
        set.upload(data, gs);
        slot = std::make_unique<gt3r::Tex1::Image>(set.decode(gs, 0));
    } catch (const FormatError&) {
        slot.reset();
    }
    return slot.get();
}

} // namespace gt4r::menu
