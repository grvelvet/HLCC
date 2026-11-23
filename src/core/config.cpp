#include "core/config.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <memory>
#include <ranges>
#include <type_traits>

namespace subs {

std::vector<std::string> Config::ParseCsvList(std::string_view v) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < v.size()) {
        size_t comma = v.find(',', pos);
        if (comma == std::string_view::npos) comma = v.size();
        std::string item = Lower(TrimView(v.substr(pos, comma - pos)));
        if (!item.empty()) out.push_back(std::move(item));
        pos = comma + 1;
    }
    return out;
}

void Config::InitDefaultExceptions() {
    sfx_creature_groups = {
        "headcrab", "zombie", "bullchicken", "houndeye", "barnacle",
        "aslave", "agrunt", "ichy", "controller", "garg", "gonarch",
        "tentacle", "x", "turret", "apache", "hassault",
        "gonome", "pitdrone", "voltigore", "shockroach", "pitworm", "geneworm"
    };
    no_occlusion_prefixes = {
        "sc_silo", "scientist/c1a3_sci_silo", "sc_idle4", "sc_runscream",
        "ba_assn", "barney/c2a3_ba_assn", "ba_launch", "barney/c2a2_ba_launch",
        "ba_tram", "tram/", "dr_", "drill/", "holo_", "holo/", "ba_holo/",
        "gm_suit", "gm_nasty", "gm_potential", "gm_stepin", "gm_otherwise",
        "gm_1choose", "gm_2choose", "gm_wise", "gm_regret", "gm_wontwork",
        "gman/gman_suit", "gman/gman_nasty", "gman/gman_potential", "gman/gman_stepin",
        "gman/gman_otherwise", "gman/gman_choose", "gman/gman_wise", "gman/gman_noreg",
        "gman/gman_nowork", "gman/gman_offer",
        "hg_chat", "hgrunt/c2a2_hg_chat", "hgrunt/c2a5_hg_abandon", "hgrunt/c2a5_hg_lebuz",
        // Opposing Force scripted/glass/locked-door scenes
        "of1a5_barn", "sc_ihavent", "sc_seenspecies", "ba_zombie2", "ba_zombie4", "sc_fear3",
        "bo_", "ops/", "ba_bomboff", "barney/of6a4_ba", "ba_storageunit",
        "ot_tellsecret", "otis/of6a3_ot", "tg_", "fgrunt/of5a3_fg", "intro_", "fgrunt/intro_",
        "misc/broken_tram",
        // Blue Shift scripted/locked-door/glass/container scenes
        "ba_sec", "sc_sec", "sc_accident", "sc_acc_die", "sc_canals", "sc_scream",
        "sc_yard_box", "sc_rescued", "ro_inside_car", "ro_yard_", "ba_power", "sc_power",
        "ro_tele_", "sc_tele_", "ro_xen_call", "rosenberg/ro_xen_call",
        "hg_canal", "hgrunt/hg_canal", "ba_canal", "ot_intro"
    };
}

bool Config::IsCreatureSfxDir(std::string_view dir) const {
    for (const auto& g : sfx_creature_groups) {
        if (dir == g) return true;
    }
    return false;
}

bool Config::IsNoOcclusionPrefix(std::string_view norm) const {
    for (const auto& p : no_occlusion_prefixes) {
        if (norm.starts_with(p)) return true;
    }
    return false;
}

std::string Config::DetectAutoLanguage() {
    if (const char* cmd = GetCommandLineA()) {
        std::string lc = Lower(cmd);
        if (lc.contains("-language russian")) return "ru";
        if (lc.contains("-language english")) return "en";
    }
    for (const char* envName : {"LANG", "LC_ALL"}) {
        char envBuf[64]{};
        DWORD n = GetEnvironmentVariableA(envName, envBuf, sizeof(envBuf));
        if (n > 0 && n < sizeof(envBuf)) {
            std::string ev = Lower(envBuf);
            if (ev.starts_with("ru") || ev.contains("russian")) return "ru";
        }
    }
    HKEY rawKey = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &rawKey) == ERROR_SUCCESS) {
        struct RegKeyCloser {
            void operator()(HKEY k) const noexcept { if (k) RegCloseKey(k); }
        };
        std::unique_ptr<std::remove_pointer_t<HKEY>, RegKeyCloser> keyGuard(rawKey);
        char buf[64]{};
        DWORD sz = sizeof(buf);
        DWORD type = 0;
        LSTATUS st = RegQueryValueExA(keyGuard.get(), "Language", nullptr, &type,
                                      reinterpret_cast<LPBYTE>(buf), &sz);
        if (st == ERROR_SUCCESS && type == REG_SZ && buf[0]) {
            return (Lower(buf) == "russian") ? "ru" : "en";
        }
    }
    return ((GetUserDefaultUILanguage() & 0xFF) == LANG_RUSSIAN) ? "ru" : "en";
}

void Config::ApplyLanguage(std::string_view lang) {
    std::string l = Lower(lang);
    if (l == "auto") l = DetectAutoLanguage();
    if (l == "ru" || l == "russian") {
        language = "ru";
        subtitles_pattern = "subtitles_ru.tsv";
        titles_pattern = "titles_ru.tsv";
    } else if (l == "en" || l == "english") {
        language = "en";
        subtitles_pattern = "subtitles_en.tsv";
        titles_pattern = "titles_en.tsv";
    }
}

std::string_view Config::TrimView(std::string_view s) noexcept {
    while (!s.empty() && static_cast<unsigned char>(s.front()) <= ' ') s.remove_prefix(1);
    while (!s.empty() && static_cast<unsigned char>(s.back()) <= ' ') s.remove_suffix(1);
    return s;
}

std::string Config::Trim(std::string_view s) {
    return std::string(TrimView(s));
}

std::string Config::Lower(std::string_view sv) {
    std::string s(sv);
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int Config::ParseInt(std::string_view val, int def) noexcept {
    val = TrimView(val);
    if (val.empty()) return def;
    int out = def;
    auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), out);
    return (ec == std::errc{}) ? out : def;
}

float Config::ParseFloat(std::string_view val, float def) noexcept {
    val = TrimView(val);
    if (val.empty()) return def;
    float out = def;
    auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), out);
    return (ec == std::errc{}) ? out : def;
}

Rgb Config::ParseRgb(std::string_view val, Rgb def) noexcept {
    std::array<int, 3> ch{def.r, def.g, def.b};
    size_t idx = 0;
    for (auto part : val | std::views::split(',')) {
        if (idx >= 3) return def;
        std::string_view sv(part.begin(), part.end());
        sv = TrimView(sv);
        int v = 0;
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
        if (ec != std::errc{} || ptr != sv.data() + sv.size()) return def;
        ch[idx++] = std::clamp(v, 0, 255);
    }
    return (idx == 3) ? Rgb{ch[0], ch[1], ch[2]} : def;
}

void Config::InitDefaultRoles() {
    auto col = [&](std::string_view k, Rgb def) {
        auto it = named_colors.find(k);
        return it != named_colors.end() ? it->second : def;
    };
    roles.clear();
    roles.push_back({"scientist/", col("color_scientist", {255,165,55}), RoleCat::NPC});
    roles.push_back({"sc_",        col("color_scientist", {255,165,55}), RoleCat::NPC});
    roles.push_back({"sci_",       col("color_scientist", {255,165,55}), RoleCat::NPC});
    roles.push_back({"cleansuit/", col("color_scientist", {255,165,55}), RoleCat::NPC});
    roles.push_back({"cs_",        col("color_scientist", {255,165,55}), RoleCat::NPC});
    roles.push_back({"rosenberg/", col("color_scientist", {255,165,55}), RoleCat::NPC});
    roles.push_back({"ro_",        col("color_scientist", {255,165,55}), RoleCat::NPC});
    roles.push_back({"simmons/",   col("color_scientist", {255,165,55}), RoleCat::NPC});
    roles.push_back({"walter/",    col("color_scientist", {255,165,55}), RoleCat::NPC});
    roles.push_back({"barney/",    col("color_guard",     {100,175,255}), RoleCat::NPC});
    roles.push_back({"ba_",        col("color_guard",     {100,175,255}), RoleCat::NPC});
    roles.push_back({"otis/",      col("color_guard",     {100,175,255}), RoleCat::NPC});
    roles.push_back({"ot_",        col("color_guard",     {100,175,255}), RoleCat::NPC});
    roles.push_back({"of1a5_",     col("color_guard",     {100,175,255}), RoleCat::NPC});
    roles.push_back({"fgrunt/",    col("color_ally",      {120,235,120}), RoleCat::NPC});
    roles.push_back({"fg_",        col("color_ally",      {120,235,120}), RoleCat::NPC});
    roles.push_back({"tg_",        col("color_ally",      {120,235,120}), RoleCat::NPC});
    roles.push_back({"mg_",        col("color_ally",      {120,235,120}), RoleCat::NPC});
    roles.push_back({"medic/",     col("color_ally",      {120,235,120}), RoleCat::NPC});
    roles.push_back({"rc_",        col("color_ally",      {120,235,120}), RoleCat::NPC});
    roles.push_back({"drill/",     col("color_drill",     {120,235,120}), RoleCat::NPC});
    roles.push_back({"dr_",        col("color_drill",     {120,235,120}), RoleCat::NPC});
    roles.push_back({"di_",        col("color_drill",     {120,235,120}), RoleCat::NPC});
    roles.push_back({"bootcamp/",  col("color_drill",     {120,235,120}), RoleCat::NPC});
    roles.push_back({"hgrunt/",    col("color_enemy",     {120,235,120}), RoleCat::NPC});
    roles.push_back({"hg_",        col("color_enemy",     {120,235,120}), RoleCat::NPC});
    roles.push_back({"ops/",       col("color_blackops",  {245,95,95}),   RoleCat::NPC});
    roles.push_back({"bo_",        col("color_blackops",  {245,95,95}),   RoleCat::NPC});
    roles.push_back({"fvox/",      col("color_hev",       {255,195,65}), RoleCat::HEV});
    roles.push_back({"hev_",       col("color_hev",       {255,195,65}), RoleCat::HEV});
    roles.push_back({"vox/",       col("color_vox",       {155,215,255}), RoleCat::VOX});
    roles.push_back({"vox_",       col("color_vox",       {155,215,255}), RoleCat::VOX});
    roles.push_back({"tride/",     col("color_vox",       {155,215,255}), RoleCat::VOX});
    roles.push_back({"tram/",      col("color_vox",       {155,215,255}), RoleCat::VOX});
    roles.push_back({"misc/broken_tram", col("color_vox", {155,215,255}), RoleCat::VOX});
    roles.push_back({"c1a0_",      col("color_vox",       {155,215,255}), RoleCat::VOX});
    roles.push_back({"radio/",     col("color_vox",       {155,215,255}), RoleCat::VOX});
    roles.push_back({"rd_",        col("color_vox",       {155,215,255}), RoleCat::VOX});
    roles.push_back({"intro_fg",   col("color_ally",      {120,235,120}), RoleCat::NPC});
    roles.push_back({"intro_dr",   col("color_drill",     {120,235,120}), RoleCat::NPC});
    roles.push_back({"intro/",     col("color_vox",       {155,215,255}), RoleCat::VOX});
    roles.push_back({"opfor_radio",col("color_vox",       {155,215,255}), RoleCat::VOX});
    roles.push_back({"gman/",      col("color_gman",      {205,185,255}), RoleCat::NPC});
    roles.push_back({"gm_",        col("color_gman",      {205,185,255}), RoleCat::NPC});
    roles.push_back({"of7a0_",     col("color_gman",      {205,185,255}), RoleCat::NPC});
    roles.push_back({"nihilanth/", col("color_gman",      {205,185,255}), RoleCat::NPC});
    roles.push_back({"holo/",      col("color_holo",      {140,245,220}), RoleCat::NPC});
    roles.push_back({"holo_",      col("color_holo",      {140,245,220}), RoleCat::NPC});
    roles.push_back({"ba_holo/",   col("color_holo",      {140,245,220}), RoleCat::NPC});
    for (const char* p : {"headcrab/","zombie/","gonome/","pitdrone/","voltigore/","shocktrooper/","shockroach/",
                          "pitworm/","geneworm/","bullchicken/","houndeye/","agrunt/","aslave/","barnacle/",
                          "controller/","garg/","gonarch/","tentacle/","turret/","apache/","hassault/",
                          "weapons/explode","weapons/mortar","weapons/mine_","common/nuke_","ambience/warn",
                          "ambience/siren","misc/ear_ringing"}) {
        roles.push_back({p, col("color_sfx_danger", {245,140,110}), RoleCat::SFX_DANGER});
    }
    for (const char* p : {"items/","player/","weapons/","debris/","buttons/","doors/","common/","ambience/","misc/"}) {
        roles.push_back({p, col("color_sfx", {185,195,205}), RoleCat::SFX});
    }
}

bool Config::ApplySettingKey(const std::string& k, std::string_view v) {
    if (k == "max_distance") max_distance = std::max(0.0f, ParseFloat(v, max_distance));
    else if (k == "glass_max_distance") glass_max_distance = std::max(0.0f, ParseFloat(v, glass_max_distance));
    else if (k == "no_occlusion_max_distance") no_occlusion_max_distance = std::max(0.0f, ParseFloat(v, no_occlusion_max_distance));
    else if (k == "min_volume") min_volume = std::clamp(ParseFloat(v, min_volume), 0.0f, 1.0f);
    else if (k == "use_attenuation_cutoff") use_attenuation_cutoff = (ParseInt(v, use_attenuation_cutoff ? 1 : 0) != 0);
    else if (k == "check_occlusion") check_occlusion = (ParseInt(v, check_occlusion ? 1 : 0) != 0);
    else if (k == "enable_vox") enable_vox = (ParseInt(v, enable_vox ? 1 : 0) != 0);
    else if (k == "enable_hev") enable_hev = (ParseInt(v, enable_hev ? 1 : 0) != 0);
    else if (k == "restore_unused_hev") restore_unused_hev = (ParseInt(v, restore_unused_hev ? 1 : 0) != 0);
    else if (k == "auto_wav_timing") auto_wav_timing = (ParseInt(v, auto_wav_timing ? 1 : 0) != 0);
    else if (k == "enable_npc") enable_npc = (ParseInt(v, enable_npc ? 1 : 0) != 0);
    else if (k == "enable_sfx") enable_sfx = (ParseInt(v, enable_sfx ? 1 : 0) != 0);
    else if (k == "sfx_max_distance") sfx_max_distance = std::max(0.0f, ParseFloat(v, sfx_max_distance));
    else if (k == "sfx_min_volume") sfx_min_volume = std::clamp(ParseFloat(v, sfx_min_volume), 0.0f, 1.0f);
    else if (k == "sfx_cooldown_ms") sfx_cooldown_ms = std::clamp(ParseInt(v, sfx_cooldown_ms), 200, 10000);
    else if (k == "max_sfx_captions") max_sfx_captions = std::clamp(ParseInt(v, max_sfx_captions), 1, 6);
    else if (k == "debug_log") debug_log = (ParseInt(v, debug_log ? 1 : 0) != 0);
    else if (k == "sfx_creature_groups") sfx_creature_groups = ParseCsvList(v);
    else if (k == "no_occlusion_prefixes") no_occlusion_prefixes = ParseCsvList(v);
    else if (k == "style") style = (Lower(v) == "hl2") ? "hl2" : "default";
    else if (k == "font_name" && !v.empty()) font_name = std::string(v);
    else if (k == "font_size") font_size = std::clamp(ParseInt(v, font_size), 0, 96);
    else if (k == "bottom_margin_pct") bottom_margin_pct = std::clamp(ParseInt(v, bottom_margin_pct), 2, 45);
    else if (k == "max_width_pct") max_width_pct = std::clamp(ParseInt(v, max_width_pct), 20, 95);
    else if (k == "bg_color") bg_color = ParseRgb(v, bg_color);
    else if (k == "bg_alpha") bg_alpha = std::clamp(ParseInt(v, bg_alpha), 0, 255);
    else if (k == "bg_radius") bg_radius = std::clamp(ParseInt(v, bg_radius), 0, 24);
    else if (k == "max_lines") max_lines = std::clamp(ParseInt(v, max_lines), 1, 10);
    else if (k == "max_lines_per_caption") max_lines_per_caption = std::clamp(ParseInt(v, max_lines_per_caption), 0, 10);
    else if (k == "hud_compact_armor") hud_compact_armor = (ParseInt(v, hud_compact_armor ? 1 : 0) != 0);
    else if (k == "hud_scale") hud_scale = std::clamp(ParseFloat(v, hud_scale), 1.0f, 2.5f);
    else if (k == "enable_gl_flashlight") enable_gl_flashlight = (ParseInt(v, enable_gl_flashlight ? 1 : 0) != 0);
    else if (k == "gl_flashlight_intensity") gl_flashlight_intensity = std::clamp(ParseFloat(v, gl_flashlight_intensity), 0.2f, 2.5f);
    else if (k == "gl_flashlight_hide_vanilla") gl_flashlight_hide_vanilla = (ParseInt(v, gl_flashlight_hide_vanilla ? 1 : 0) != 0);
    else if (k == "language" && !v.empty()) ApplyLanguage(v);
    else if (k == "fallback_dir" && !v.empty()) fallback_dir = std::string(v);
    else if (k == "subtitles_pattern" && !v.empty()) subtitles_pattern = std::string(v);
    else if (k == "titles_pattern" && !v.empty()) titles_pattern = std::string(v);
    else return false;
    return true;
}

std::expected<void, std::string> Config::LoadIni(const std::string& path,
                                                 std::string_view activeGameDir,
                                                 std::string_view liblistFallbackDir) {
    *this = Config{};
    InitDefaultExceptions();
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        InitDefaultRoles();
        return std::unexpected("Could not open INI: " + path);
    }
    named_colors.clear();
    roles.clear();
    bool has_roles_section = false;
    std::string activeLower = Lower(activeGameDir);
    std::string fbLower = Lower(liblistFallbackDir);
    std::vector<std::pair<std::string, std::string>> fbModOverrides;
    std::vector<std::pair<std::string, std::string>> activeModOverrides;
    std::string section, line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string_view t = TrimView(line);
        if (t.empty() || t.front() == ';' || t.front() == '#') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = Lower(t.substr(1, t.size() - 2));
            if (section == "roles") has_roles_section = true;
            continue;
        }
        size_t eq = t.find('=');
        if (eq == std::string_view::npos) continue;
        std::string k = Lower(TrimView(t.substr(0, eq)));
        std::string_view v = TrimView(t.substr(eq + 1));

        if (section == "filtering" || section == "visual" || section == "hud" || section == "files") {
            ApplySettingKey(k, v);
        } else if (section.starts_with("mod:")) {
            std::string modName = Lower(TrimView(std::string_view(section).substr(4)));
            if (!fbLower.empty() && modName == fbLower && modName != activeLower) {
                fbModOverrides.emplace_back(std::move(k), std::string(v));
            } else if (!activeLower.empty() && modName == activeLower) {
                activeModOverrides.emplace_back(std::move(k), std::string(v));
            }
        } else if (section == "colors") {
            Rgb c = ParseRgb(v, default_color);
            named_colors[k] = c;
            if (k == "color_default") default_color = c;
        } else if (section == "roles") {
            RoleRule rule;
            rule.prefix = std::move(k);
            std::string_view colPart = v;
            std::string catPart = "npc";
            int commas = static_cast<int>(std::ranges::count(v, ','));
            if (commas == 3 || commas == 1) {
                size_t last = v.rfind(',');
                colPart = TrimView(v.substr(0, last));
                catPart = Lower(TrimView(v.substr(last + 1)));
            }
            auto it = named_colors.find(Lower(colPart));
            rule.color = (it != named_colors.end()) ? it->second : ParseRgb(colPart, default_color);
            if (catPart == "vox") rule.cat = RoleCat::VOX;
            else if (catPart == "hev") rule.cat = RoleCat::HEV;
            else if (catPart == "sfx_danger") rule.cat = RoleCat::SFX_DANGER;
            else if (catPart == "sfx") rule.cat = RoleCat::SFX;
            else rule.cat = RoleCat::NPC;
            roles.push_back(std::move(rule));
        }
    }
    for (const auto& [ok, ov] : fbModOverrides) {
        ApplySettingKey(ok, ov);
    }
    for (const auto& [ok, ov] : activeModOverrides) {
        ApplySettingKey(ok, ov);
    }
    if (!has_roles_section || roles.empty()) {
        InitDefaultRoles();
    }
    return {};
}

std::pair<Rgb, RoleCat> Config::Classify(std::string_view normKey, std::string_view text) const {
    for (const auto& r : roles) {
        if (normKey.starts_with(r.prefix)) {
            return {r.color, r.cat};
        }
    }
    auto getCol = [&](std::string_view k, Rgb def) {
        auto it = named_colors.find(k);
        return it != named_colors.end() ? it->second : def;
    };
    if (text.starts_with("[VOX]") || text.starts_with("[Intercom]") ||
        text.starts_with("[\xD0\x98\xD0\xBD\xD1\x82\xD0\xB5\xD1\x80\xD0\xBA\xD0\xBE\xD0\xBC]") ||
        text.starts_with("[\xD0\xA1\xD0\xB8\xD1\x81\xD1\x82\xD0\xB5\xD0\xBC\xD0\xB0 \xD0\xBE\xD0\xBF\xD0\xBE\xD0\xB2\xD0\xB5\xD1\x89\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F]")) {
        return {getCol("color_vox", {155,215,255}), RoleCat::VOX};
    }
    if (text.starts_with("[HEV]")) {
        return {getCol("color_hev", {255,195,65}), RoleCat::HEV};
    }
    return {default_color, RoleCat::NPC};
}

bool Config::IsCategoryEnabled(RoleCat cat) const noexcept {
    if (cat == RoleCat::VOX) return enable_vox;
    if (cat == RoleCat::HEV) return enable_hev;
    if (cat == RoleCat::SFX || cat == RoleCat::SFX_DANGER) return enable_sfx;
    return enable_npc;
}

} // namespace subs
