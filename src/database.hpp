#pragma once
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <expected>
#include <fstream>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>
#include "config.hpp"

namespace subs {

struct SubtitleEntry {
    uint32_t duration_ms = 2000;
    std::string text;
};

class Database {
public:
    StringMap<SubtitleEntry> subtitles;
    StringMap<std::string> titles;

    static std::string NormalizeKey(std::string_view sv) {
        while (!sv.empty() && (sv.front() == '!' || sv.front() == '*' || sv.front() == '#' ||
                               sv.front() == '@' || sv.front() == '/' || sv.front() == '\\')) {
            sv.remove_prefix(1);
        }
        std::string s(sv);
        for (char& c : s) {
            c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (s.starts_with("sound/")) s.erase(0, 6);
        if (s.ends_with(".wav") || s.ends_with(".mp3")) s.erase(s.size() - 4);
        return s;
    }

    static std::string UnescapeNewlines(std::string_view s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
                out.push_back('\n');
                ++i;
            } else {
                out.push_back(s[i]);
            }
        }
        return out;
    }

    static std::string_view StripBom(std::string_view sv) noexcept {
        if (sv.starts_with("\xEF\xBB\xBF")) sv.remove_prefix(3);
        return sv;
    }

    std::expected<size_t, std::string> LoadSubtitlesTsv(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return std::unexpected("Could not open subtitles TSV: " + path);
        size_t count = 0;
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::string_view sv = StripBom(line);
            if (sv.empty() || sv.front() == '#') continue;

            std::string_view cols[3]{};
            size_t n = 0;
            for (auto part : sv | std::views::split('\t')) {
                if (n < 2) {
                    cols[n++] = std::string_view(part.begin(), part.end());
                } else {
                    cols[2] = sv.substr(static_cast<size_t>(part.begin() - sv.data()));
                    n = 3;
                    break;
                }
            }
            if (n < 3) continue;

            std::string key = NormalizeKey(cols[0]);
            uint32_t dur = 0;
            std::string_view durSv = Config::TrimView(cols[1]);
            if (!durSv.empty()) {
                std::from_chars(durSv.data(), durSv.data() + durSv.size(), dur);
            }
            if (dur == 0) dur = 2000;
            std::string txt = UnescapeNewlines(cols[2]);
            if (!key.empty() && !txt.empty()) {
                subtitles[std::move(key)] = {dur, std::move(txt)};
                ++count;
            }
        }
        return count;
    }

    static std::string UpperTrimTitleKey(std::string_view raw) {
        std::string out(Config::TrimView(raw));
        for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return out;
    }

    std::expected<size_t, std::string> LoadTitlesTsv(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return std::unexpected("Could not open titles TSV: " + path);
        size_t count = 0;
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::string_view sv = StripBom(line);
            if (sv.empty() || sv.front() == '#') continue;
            size_t t1 = sv.find('\t');
            if (t1 == std::string_view::npos) continue;
            std::string key = UpperTrimTitleKey(sv.substr(0, t1));
            std::string txt = UnescapeNewlines(sv.substr(t1 + 1));
            if (!key.empty()) {
                titles[std::move(key)] = std::move(txt);
                ++count;
            }
        }
        return count;
    }

    static std::vector<std::string> GlobDir(const std::string& dir, const std::string& pattern) {
        std::vector<std::string> files;
        WIN32_FIND_DATAA fd{};
        std::string query = dir + "\\" + pattern;
        HANDLE raw = FindFirstFileA(query.c_str(), &fd);
        if (raw == INVALID_HANDLE_VALUE) return files;
        struct FindCloser {
            void operator()(HANDLE h) const noexcept {
                if (h && h != INVALID_HANDLE_VALUE) FindClose(h);
            }
        };
        std::unique_ptr<void, FindCloser> hGuard(raw);
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                files.push_back(dir + "\\" + fd.cFileName);
            }
        } while (FindNextFileA(hGuard.get(), &fd));
        std::ranges::sort(files);
        return files;
    }

    static std::string ReadLiblistFallbackDir(const std::string& dir) {
        std::ifstream f(dir + "\\liblist.gam");
        if (!f.is_open()) return "";
        std::string line;
        while (std::getline(f, line)) {
            std::string_view sv = Config::TrimView(line);
            if (sv.size() < 12 || Config::Lower(sv.substr(0, 12)) != "fallback_dir") continue;
            size_t q1 = sv.find('"', 12);
            if (q1 == std::string_view::npos) continue;
            size_t q2 = sv.find('"', q1 + 1);
            if (q2 != std::string_view::npos && q2 > q1 + 1) {
                return std::string(sv.substr(q1 + 1, q2 - q1 - 1));
            }
        }
        return "";
    }

    void ReloadAll(const std::string& exeDir, const std::string& gameDir, const Config& cfg) {
        subtitles.clear();
        titles.clear();

        auto loadDir = [&](const std::string& modName) {
            if (modName.empty()) return;
            std::string dir = exeDir + "\\" + modName;
            for (const auto& f : GlobDir(dir, cfg.subtitles_pattern)) (void)LoadSubtitlesTsv(f);
            for (const auto& f : GlobDir(dir, cfg.titles_pattern)) (void)LoadTitlesTsv(f);
        };

        std::string baseFallback = Config::Lower(cfg.fallback_dir);
        std::string gameLower = Config::Lower(gameDir);
        loadDir(cfg.fallback_dir);

        if (!gameDir.empty() && gameLower != baseFallback) {
            std::string libFallback = ReadLiblistFallbackDir(exeDir + "\\" + gameDir);
            std::string libLower = Config::Lower(libFallback);
            if (!libFallback.empty() && libLower != baseFallback && libLower != gameLower) {
                loadDir(libFallback);
            }
            loadDir(gameDir);
        }
    }

    const SubtitleEntry* FindSubtitle(std::string_view rawKey, std::string& outNorm) const {
        outNorm = NormalizeKey(rawKey);
        if (auto it = subtitles.find(outNorm); it != subtitles.end()) return &it->second;
        std::string_view nv(outNorm);
        if (size_t sl = nv.rfind('/'); sl != std::string_view::npos) {
            if (auto it2 = subtitles.find(nv.substr(sl + 1)); it2 != subtitles.end()) {
                return &it2->second;
            }
        }
        return nullptr;
    }
};

} // namespace subs
