#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "core/config.hpp"

namespace subs {

struct SubtitleEntry {
    uint32_t duration_ms = 2000;
    std::string text;
};

class Database {
public:
    StringMap<SubtitleEntry> subtitles;
    StringMap<std::string> titles;

    static std::string NormalizeKey(std::string_view sv);
    static std::string UnescapeNewlines(std::string_view s);
    static std::string_view StripBom(std::string_view sv) noexcept;
    static std::string UpperTrimTitleKey(std::string_view raw);
    static std::vector<std::string> GlobDir(const std::string& dir, const std::string& pattern);
    static std::string ReadLiblistFallbackDir(const std::string& dir);

    std::expected<size_t, std::string> LoadSubtitlesTsv(const std::string& path);
    std::expected<size_t, std::string> LoadTitlesTsv(const std::string& path);
    void ReloadAll(const std::string& exeDir, const std::string& gameDir, const Config& cfg);
    const SubtitleEntry* FindSubtitle(std::string_view rawKey, std::string& outNorm) const;
};

} // namespace subs
