#include "gt4r/formats/db/mod_makers.h"

#include "base/config.h"
#include "base/file.h"

#include <algorithm>

namespace gt4r::db {

using gt3r::IniDocument;
using gt3r::to_lower;
using gt3r::trim;

namespace {

// A value without a trailing " ; comment" / " # comment".
std::string value_text(std::string_view v) {
    for (size_t i = 1; i < v.size(); ++i)
        if ((v[i] == ';' || v[i] == '#') && (v[i - 1] == ' ' || v[i - 1] == '\t')) {
            v = v.substr(0, i);
            break;
        }
    return std::string(trim(v));
}

bool valid_key(const std::string& k) {
    if (k.empty() || k.size() > 30) return false;
    for (char c : k)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

// GT3 Revisited's maker files name a region of its dealers' map.
std::string country_of_region(const std::string& r) {
    static const std::pair<const char*, const char*> k[] = {{"jp", "JP"}, {"us", "US"}, {"gr", "DE"}, {"fr", "FR"},
                                                            {"it", "IT"}, {"uk", "GB"}, {"bel", "BE"}, {"aus", "AU"}};
    for (const auto& [from, to] : k)
        if (r == from) return to;
    return {};
}

int next_free(const std::map<int, std::string>& taken, const std::vector<ModMakerDef>& given, int ModMakerDef::*field) {
    int n = taken.empty() ? 1 : taken.rbegin()->first + 1;
    for (const ModMakerDef& m : given) n = std::max(n, m.*field + 1);
    return n < 256 ? n : -1;
}

} // namespace

std::string maker_match_key(std::string_view name) {
    std::string s = to_lower(std::string(trim(name)));
    std::erase_if(s, [](char c) { return c == ' ' || c == '-' || c == '_' || c == '.'; });
    return s;
}

std::string ModMakerDef::folder() const { return mod_folder_of(file, "makers"); }

std::optional<ModMakerDef> parse_mod_maker(std::string_view text, const std::string& key, std::string* error, std::vector<std::string>* warnings) {
    auto fail = [&](const std::string& m) -> std::optional<ModMakerDef> {
        if (error) *error = m;
        return std::nullopt;
    };
    ModMakerDef m;
    m.key = to_lower(key);
    if (!valid_key(m.key)) return fail("the file's name is the maker's key: letters, digits and _ only (" + key + ")");
    const IniDocument ini = IniDocument::parse(text);
    std::string region;
    for (const IniDocument::Section& sec : ini.sections())
        for (const auto& [key_raw, val_raw] : sec.entries) {
            const std::string k = to_lower(key_raw), v = value_text(val_raw);
            if (to_lower(sec.name) != "maker") {
                if (warnings) warnings->push_back("[" + sec.name + "] " + k + ": not read (a maker file has [maker] only)");
                continue;
            }
            if (k == "name") m.name = v;
            else if (k == "country") {
                m.country = v;
                for (char& c : m.country) c = char(std::toupper(static_cast<unsigned char>(c)));
            } else if (k == "region") region = to_lower(v);
            else if (k == "icon") m.icon = v;
            else if (k == "logo") m.logo = v;
            else if (k == "description") m.description = v;
            else if (k == "background") {
                if (warnings) warnings->push_back("[maker] background: not used (the dealer page's backdrop is its country's)");
            } else if (warnings) warnings->push_back("[maker] " + k + ": not a key of a maker file");
        }
    if (m.name.empty()) return fail("[maker] needs a name");
    if (m.country.empty() && !region.empty()) {
        m.country = country_of_region(region);
        if (m.country.empty()) return fail("[maker] region " + region + ": not one of jp, us, gr, fr, it, uk, bel, aus");
    }
    if (m.country.empty()) return fail("[maker] needs a country (JP, US, GB, DE, FR, IT, AU, KR, BE, NL, SE or ES)");
    return m;
}

ModMakers ModMakers::read(std::string_view list, const std::map<int, std::string>& makers, const std::map<int, std::string>& tuners,
                          const std::map<int, std::string>& countries, const ModMakers* before) {
    ModMakers out;
    if (before) out = *before;
    size_t at = 0;
    while (at < list.size()) {
        size_t end = list.find('\n', at);
        if (end == std::string_view::npos) end = list.size();
        const std::string_view line = list.substr(at, end - at);
        at = end + 1;
        const size_t tab = line.find('\t');
        if (tab == std::string_view::npos) continue;
        const std::string mod(line.substr(0, tab)), file(line.substr(tab + 1));
        if (!mod_file_in(file, "makers") || file.size() < 5 || to_lower(file.substr(file.size() - 4)) != ".ini") continue;
        if (before && std::any_of(before->statuses_.begin(), before->statuses_.end(), [&](const Status& s) { return s.file == file; })) continue;
        Status st;
        st.mod = mod, st.file = file;
        const size_t slash = mod_file_name_at(file);
        st.key = to_lower(file.substr(slash, file.size() - 4 - slash));
        const std::optional<Bytes> text = gt3r::read_host_file(file);
        std::optional<ModMakerDef> m;
        if (!text) st.error = "the file is not read";
        else m = parse_mod_maker(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()), st.key, &st.error, &st.warnings);
        if (m) {
            m->mod = mod, m->file = file, st.name = m->name;
            const auto taken = [&](const std::map<int, std::string>& l) {
                return std::any_of(l.begin(), l.end(), [&](const auto& e) { return e.second == m->key; });
            };
            for (const auto& [n, code] : countries)
                if (code == m->country) m->country_number = n;
            if (taken(makers) || taken(tuners)) st.error = "'" + m->key + "' is a maker of the game already: its cars name it in [car] maker";
            else if (!countries.empty() && m->country_number < 0) st.error = "country " + m->country + " is not one of the game's";
            else if (const auto it = std::find_if(out.makers_.begin(), out.makers_.end(), [&](const ModMakerDef& o) { return o.key == m->key; }); it != out.makers_.end()) {
                if (before && before->by_key(m->key)) st.error = "a maker of that key is in the game already";
                else m->maker = it->maker, m->tuner = it->tuner, *it = *m; // (a later mod's file of the same key)
            } else {
                m->maker = next_free(makers, out.makers_, &ModMakerDef::maker);
                m->tuner = next_free(tuners, out.makers_, &ModMakerDef::tuner);
                if (m->maker < 0 || m->tuner < 0) st.error = "no number left for another maker";
                else out.makers_.push_back(*m);
            }
        }
        out.statuses_.push_back(std::move(st));
    }
    return out;
}

const ModMakerDef* ModMakers::named(std::string_view name) const {
    const std::string want = maker_match_key(name);
    if (want.empty()) return nullptr;
    for (const ModMakerDef& m : makers_)
        if (maker_match_key(m.name) == want || maker_match_key(m.key) == want) return &m;
    return nullptr;
}

const ModMakerDef* ModMakers::by_key(std::string_view key) const {
    for (const ModMakerDef& m : makers_)
        if (m.key == key) return &m;
    return nullptr;
}

const ModMakerDef* ModMakers::by_maker(int number) const {
    for (const ModMakerDef& m : makers_)
        if (m.maker == number) return &m;
    return nullptr;
}

std::optional<ModCars::Picture> ModMakers::picture(std::string_view path) const {
    while (!path.empty() && path[0] == '/') path.remove_prefix(1);
    const bool pack = path.rfind("image/", 0) == 0;
    if (pack) path.remove_prefix(6);
    const std::string_view ext = pack ? ".png" : ".img";
    if (path.rfind("piece/", 0) != 0 || path.size() < 10 || path.substr(path.size() - 4) != ext) return std::nullopt;
    const size_t slash = path.find('/', 6);
    if (slash == std::string_view::npos) return std::nullopt;
    const std::string_view folder = path.substr(6, slash - 6), key = path.substr(slash + 1, path.size() - 4 - slash - 1);
    const ModMakerDef* m = by_key(key);
    if (!m) return std::nullopt;
    auto own = [&](const std::string& p) { return p.empty() ? std::string() : p[0] == '/' || p.find("..") != std::string::npos ? std::string() : m->folder() + p; };
    ModCars::Picture out;
    if (folder == "tuner_logo2") out.png = own(m->icon);
    else if (folder == "tuner_logo" || folder == "tuneshop_logo") out.png = own(m->logo);
    else return std::nullopt;
    return out;
}

} // namespace gt4r::db
