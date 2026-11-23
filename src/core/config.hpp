#pragma once

#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace subs {

struct TransparentStringHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
};

template <typename V>
using StringMap = std::unordered_map<std::string, V, TransparentStringHash, std::equal_to<>>;

struct Rgb {
    int r = 240, g = 242, b = 245;
};

enum class RoleCat {
    NPC = 0,
    VOX = 1,
    HEV = 2,
    SFX = 3,
    SFX_DANGER = 4
};

struct RoleRule {
    std::string prefix;
    Rgb color;
    RoleCat cat = RoleCat::NPC;
};

struct Config {
    // [Filtering]
    float max_distance = 650.0f;
    float glass_max_distance = 320.0f;
    float no_occlusion_max_distance = 1500.0f;
    float min_volume = 0.05f;
    bool check_occlusion = true;
    bool enable_vox = true;
    bool enable_hev = true;
    bool restore_unused_hev = true;
    bool auto_wav_timing = false;
    bool enable_npc = true;
    bool enable_sfx = true;
    float sfx_max_distance = 550.0f;
    float sfx_min_volume = 0.20f;
    int sfx_cooldown_ms = 1800;
    int max_sfx_captions = 2;
    bool debug_log = false;
    std::vector<std::string> sfx_creature_groups;
    std::vector<std::string> no_occlusion_prefixes;

    // [Visual]
    std::string style = "default"; // "default" or "hl2"
    std::string font_name = "Tahoma";
    int font_size = 0;
    int bottom_margin_pct = 13;
    int max_width_pct = 62;
    Rgb bg_color{0, 0, 0};
    int bg_alpha = 128;
    int bg_radius = 8;
    int max_lines = 4;
    int max_lines_per_caption = 3;

    // [HUD]
    bool hud_compact_armor = false;
    float hud_scale = 1.0f;
    bool enable_gl_flashlight = true;
    float gl_flashlight_intensity = 1.0f;
    bool gl_flashlight_hide_vanilla = true;

    // [Files]
    std::string language = "auto";
    std::string fallback_dir = "valve";
    std::string subtitles_pattern = "subtitles*.tsv";
    std::string titles_pattern = "titles*.tsv";

    // [Colors]
    StringMap<Rgb> named_colors;
    Rgb default_color{240, 242, 245};

    // [Roles]
    std::vector<RoleRule> roles;

    static std::vector<std::string> ParseCsvList(std::string_view v);
    static std::string DetectAutoLanguage();
    static std::string_view TrimView(std::string_view s) noexcept;
    static std::string Trim(std::string_view s);
    static std::string Lower(std::string_view sv);
    static int ParseInt(std::string_view val, int def) noexcept;
    static float ParseFloat(std::string_view val, float def) noexcept;
    static Rgb ParseRgb(std::string_view val, Rgb def) noexcept;

    void InitDefaultExceptions();
    bool IsCreatureSfxDir(std::string_view dir) const;
    bool IsNoOcclusionPrefix(std::string_view norm) const;
    void ApplyLanguage(std::string_view lang);
    void InitDefaultRoles();
    bool ApplySettingKey(const std::string& k, std::string_view v);

    /**
     * @brief Loads and validates subtitles.ini with safe fallback defaults.
     *
     * Supports per-mod override sections [Mod:<gameDir>] applied after global
     * sections (liblist fallback_dir first, then active gameDir) so mod-specific
     * behavior is data-driven rather than hardcoded in C++.
     */
    std::expected<void, std::string> LoadIni(const std::string& path,
                                             std::string_view activeGameDir = {},
                                             std::string_view liblistFallbackDir = {});

    std::pair<Rgb, RoleCat> Classify(std::string_view normKey, std::string_view text = {}) const;
    bool IsCategoryEnabled(RoleCat cat) const noexcept;
};

} // namespace subs
