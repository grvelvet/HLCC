#pragma once

/**
 * @file caption_state.hpp
 * @brief Pure subtitle runtime state (no OpenGL or GoldSrc dependencies).
 *
 * Holds active captions, localized chapter titles, INI/TSV databases,
 * game-time clock state, cooldowns, and thread synchronization.
 */

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/config.hpp"
#include "core/database.hpp"

namespace core {

struct CaptionPage {
    std::wstring text;
    ULONGLONG start_ms = 0;
    ULONGLONG end_ms = 0;
};

struct ActiveCaption {
    std::wstring text;
    subs::Rgb color;
    subs::RoleCat cat = subs::RoleCat::NPC;
    ULONGLONG start_ms = 0;    // in CaptionState::gameMs units
    ULONGLONG expire_ms = 0;   // in CaptionState::gameMs units
    std::string key;           // normalized sound key
    int entnum = -1;           // emitting entity (-1 = unknown)
    int channel = -1;          // engine sound channel
    bool isGenericNpc = false; // true for repeatable NPCs (Scientist, Security, HECU, ...)
    int speakerNum = 0;        // >0 when 2+ distinct entities of the same generic role speak in a scene
    std::wstring speakerTag;   // base tag without brackets (e.g. L"Учёный")
    bool hasOrigin = false;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    float fadeDist = 0.0f;     // >0: smoothly fade out if player moves beyond this distance
    std::vector<CaptionPage> pages;
    int cachedWrapW = 0;
    int cachedMaxLines = -1;
};

struct SpeakerGroupTrack {
    std::vector<int> entnums;
    ULONGLONG lastSeenMs = 0;
};

struct ActiveTitle {
    std::string key;
    std::wstring text;
    float x = -1.0f;
    float y = -1.0f;
    uint8_t r = 220, g = 220, b = 220;
    ULONGLONG start_ms = 0;
    ULONGLONG fadein_ms = 200;
    ULONGLONG hold_ms = 3000;
    ULONGLONG fadeout_ms = 1000;
};

inline bool IsSfxCat(subs::RoleCat c) noexcept {
    return c == subs::RoleCat::SFX || c == subs::RoleCat::SFX_DANGER;
}

struct CaptionState {
    SRWLOCK lock = SRWLOCK_INIT;
    subs::Config cfg;
    subs::Database db;

    std::string exeDir;
    std::string activeGameDir = "valve";
    std::string iniPath;
    FILETIME iniMtime{};
    DWORD lastIniCheckTick = 0;

    std::deque<ActiveCaption> captions;
    std::vector<ActiveTitle> activeTitles;
    std::string lastCapKey;
    ULONGLONG lastCapMs = 0;
    std::unordered_map<std::string, ULONGLONG> sfxCooldowns;
    std::unordered_map<std::wstring, SpeakerGroupTrack> speakerGroups;
    std::unordered_map<std::string, uint32_t> wavDurationCache;
    std::unordered_set<std::string> loggedMissingSounds;

    // Game-time clock (ms): advances only while client time advances,
    // so captions freeze while the game is paused / in the menu.
    ULONGLONG gameMs = 1;
    float clockPrevTime = -1.0f;
    float lastClientTime = 0.0f;
    ULONGLONG lastHudRedrawRealMs = 0;
    ULONGLONG lastTimeAdvanceRealMs = 0;

    std::string SfxGroupKey(std::string_view norm) const;
    void AssignSpeakerNumberLocked(ActiveCaption& cap, ULONGLONG nowMs);
    void ClearBuffersLocked();
    size_t DropForChannelLocked(int entnum, int channel, const std::string* keyFilter);
    bool IsGamePausedLocked(ULONGLONG nowRealMs) const noexcept;
    void ReloadAllFromDisk(bool reloadDb);
    void CheckIniHotReload();
};

} // namespace core
