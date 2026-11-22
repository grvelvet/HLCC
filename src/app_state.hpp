#pragma once

/**
 * @file app_state.hpp
 * @brief Shared runtime state, locks, game-mode detection, and INI/TSV reload helpers.
 *
 * Threading model:
 *  - Audio hooks (hw.dll) and render/HUD hooks (client.dll / opengl32.dll) share
 *    g_captions, g_activeTitles, g_sfxCooldowns, g_cfg, and g_db under SRWLOCK g_lock.
 *  - g_gameMs advances only while client time advances so captions freeze on pause.
 *
 * Extending for new mods:
 *  - Most mod behavior (roles, colors, no-occlusion scenes, creature SFX groups) is
 *    configured via subtitles.ini + <mod>/subtitles_*.tsv without recompiling.
 */

#include <windows.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "goldsrc.hpp"
#include "hook_x86.hpp"
#include "config.hpp"
#include "database.hpp"
#include "gl_font.hpp"

namespace {

struct CaptionPage {
    std::wstring text;
    ULONGLONG start_ms = 0;
    ULONGLONG end_ms = 0;
};

struct ActiveCaption {
    std::wstring text;
    subs::Rgb color;
    subs::RoleCat cat = subs::RoleCat::NPC;
    ULONGLONG start_ms = 0;    // in g_gameMs units
    ULONGLONG expire_ms = 0;   // in g_gameMs units
    std::string key;           // normalized sound key
    int entnum = -1;           // emitting entity (-1 = unknown)
    int channel = -1;          // engine sound channel
    bool hasOrigin = false;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    float fadeDist = 0.0f;     // >0: smoothly fade out if player moves beyond this distance
    std::vector<CaptionPage> pages;
    int cachedWrapW = 0;
    int cachedMaxLines = -1;
};

// GoldSrc sound channels / flags
constexpr int kChanAuto   = 0;
constexpr int kChanStatic = 6;
constexpr int kSndChangeVol   = 0x40;
constexpr int kSndStop        = 0x20;
constexpr int kSndChangePitch = 0x80;

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

SRWLOCK g_lock = SRWLOCK_INIT;
subs::Config g_cfg;
subs::Database g_db;

std::string g_exeDir;
std::string g_activeGameDir = "valve";
std::string g_iniPath;
FILETIME g_iniMtime{};
DWORD g_lastIniCheckTick = 0;
bool g_logOpened = false;

std::deque<ActiveCaption> g_captions;
std::vector<ActiveTitle> g_activeTitles;
std::string g_lastCapKey;
ULONGLONG g_lastCapMs = 0;
std::unordered_map<std::string, ULONGLONG> g_sfxCooldowns;
std::unordered_map<std::string, uint32_t> g_wavDurationCache;
std::unordered_set<std::string> g_loggedMissingSounds;

bool IsSfxCat(subs::RoleCat c) noexcept {
    return c == subs::RoleCat::SFX || c == subs::RoleCat::SFX_DANGER;
}

std::string SfxGroupKey(std::string_view norm) {
    if (size_t sl = norm.find('/'); sl != std::string_view::npos) {
        std::string_view dir = norm.substr(0, sl);
        if (g_cfg.IsCreatureSfxDir(dir)) return std::string(dir);
    }
    while (!norm.empty() && (std::isdigit(static_cast<unsigned char>(norm.back())) || norm.back() == '_')) {
        norm.remove_suffix(1);
    }
    return std::string(norm);
}
float g_lastClientTime = 0.0f;
bool g_resetRequested = false;
// Game-time clock (ms): advances only while client time advances,
// so captions freeze while the game is paused / in the menu.
ULONGLONG g_gameMs = 1;
float g_clockPrevTime = -1.0f;
bool g_inClientHudCall = false;
int g_prevHealthMaxX = 0;
int g_curHealthMaxX = 0;
int g_prevArmorMinX = 100000;
int g_curArmorMinX = 100000;
bool g_flashlightMsgOn = false;
ULONGLONG g_dlightFlashMs = 0;
int g_dlightFlashStreak = 0;
ULONGLONG g_swayLastRealMs = 0;
dlight_t g_dummyFlashDlight{};
using FnAllocDlight = dlight_t*(__cdecl*)(int key);
FnAllocDlight g_origAllocDlight = nullptr;

inline bool IsFlashlightActiveLocked() noexcept {
    if (g_flashlightMsgOn) return true;
    ULONGLONG now = GetTickCount64();
    return (g_dlightFlashStreak >= 2 && g_dlightFlashMs != 0 && (now - g_dlightFlashMs) < 160);
}

void ClearCaptionBuffersLocked() {
    g_captions.clear();
    g_activeTitles.clear();
    g_lastCapKey.clear();
    g_lastCapMs = 0;
    g_sfxCooldowns.clear();
    g_flashlightMsgOn = false;
    g_dlightFlashMs = 0;
    g_dlightFlashStreak = 0;
}

// Removes captions emitted by the same entity on the same channel
// (the engine replaces/stops that sound, so its caption must go too).
// Never drop HEV suit captions on channel replacement unless keyFilter matches explicitly.
size_t DropCaptionsForChannelLocked(int entnum, int channel, const std::string* keyFilter) {
    size_t dropped = std::erase_if(g_captions, [&](const ActiveCaption& c) {
        if (c.entnum != entnum || c.channel != channel) return false;
        if (!keyFilter && c.cat == subs::RoleCat::HEV) return false;
        return !keyFilter || c.key == *keyFilter;
    });
    if (keyFilter ? (g_lastCapKey == *keyFilter) : true) {
        g_lastCapKey.clear();
    }
    return dropped;
}

cl_enginefunc_t* g_engfuncs = nullptr;
glfont::GlApi g_gl;
glfont::FontAtlas g_atlas;
glfont::FontAtlas g_voxAtlas;
glfont::FontAtlas g_speakerAtlas;
glfont::FontAtlas g_titleAtlas;
int g_screenW = 0;
int g_screenH = 0;
float g_viewOrg[3] = {0, 0, 0};
float g_viewAngles[3] = {0, 0, 0};
bool g_hasViewOrg = false;
ULONGLONG g_lastHudRedrawRealMs = 0;
ULONGLONG g_lastTimeAdvanceRealMs = 0;

bool g_hwHooked = false;
bool g_clientHooked = false;
bool g_glHooked = false;

using FnLoadLibraryA   = HMODULE(WINAPI*)(LPCSTR);
using FnStartSound     = void(__cdecl*)(int entnum, int entchannel, sfx_t* sfx, float* origin, float fvol, float attenuation, int flags, int pitch);
using FnInitialize     = int(__cdecl*)(cl_enginefunc_t* pEnginefuncs, int iVersion);
using FnHudRedraw      = int(__cdecl*)(float time, int intermission);
using FnHudVidInit     = int(__cdecl*)(void);
using FnGetScreenInfo  = int(__cdecl*)(SCREENINFO* pscrinfo);
using FnCalcRefdef     = void(__cdecl*)(ref_params_t* pparams);
using FnHudReset       = void(__cdecl*)(void);
using FnTextMsgGet     = client_textmessage_t*(__cdecl*)(const char* pName);
using FnWglSwapBuffers = BOOL(WINAPI*)(HDC);
using FnLoadgame       = void(__cdecl*)(void);
using FnHookUserMsg    = int(__cdecl*)(const char* szMsgName, void* pfn);
using FnUserMsg        = int(__cdecl*)(const char* pszName, int iSize, void* pbuf);

FnLoadLibraryA   g_origLoadLibraryA = nullptr;
FnStartSound     g_origDynSound = nullptr;
FnStartSound     g_origStatSound = nullptr;
FnInitialize     g_origInitialize = nullptr;
FnHudRedraw      g_origHudRedraw = nullptr;
FnHudVidInit     g_origHudVidInit = nullptr;
FnGetScreenInfo  g_origGetScreenInfo = nullptr;
FnCalcRefdef     g_origCalcRefdef = nullptr;
FnHudReset       g_origHudReset = nullptr;
FnTextMsgGet     g_origTextMsgGet = nullptr;
FnWglSwapBuffers g_origWglSwapBuffers = nullptr;
FnLoadgame       g_origLoadgame = nullptr;
FnHookUserMsg    g_origHookUserMsg = nullptr;
FnUserMsg        g_origMsgBattery = nullptr;
FnUserMsg        g_origMsgHealth = nullptr;
FnUserMsg        g_origMsgDamage = nullptr;
FnUserMsg        g_origMsgWeapPickup = nullptr;
FnUserMsg        g_origMsgFlashlight = nullptr;
using FnSprLoad  = int(__cdecl*)(const char*);
using FnSprSet   = void(__cdecl*)(int, int, int, int);
using FnSprDraw  = void(__cdecl*)(int, int, int, const void*);
using FnFillRgba = void(__cdecl*)(int, int, int, int, int, int, int, int);
using FnDrawChar = int(__cdecl*)(int, int, int, int, int, int);
using FnTriBegin = void(__cdecl*)(int);
using FnTriEnd   = void(__cdecl*)(void);
FnSprLoad        g_origSprLoad = nullptr;
FnSprSet         g_origSprSet = nullptr;
FnSprDraw        g_origSprDraw = nullptr;
FnSprDraw        g_origSprDrawHoles = nullptr;
FnSprDraw        g_origSprDrawAdditive = nullptr;
FnFillRgba       g_origFillRgba = nullptr;
FnDrawChar       g_origDrawChar = nullptr;
FnTriBegin       g_origTriBegin = nullptr;
FnTriEnd         g_origTriEnd = nullptr;
std::unordered_set<int> g_hudSprHandles;
bool             g_curSprIsHud = false;

enum HevCue {
    kHevArmorGone = 0,
    kHevPowerRestored,
    kHevSuitDmg,
    kHevSuitCrit,
    kHevFire,
    kHevShock,
    kHevFracture,
    kHevLaceration,
    kHevBloodLoss,
    kHevSeekMedic,
    kHevCueCount
};

bool g_hevVoiceEnabled = true;
int g_prevBattery = -1;
int g_prevHealth = -1;
ULONGLONG g_lastNativeHevMs = 0;
ULONGLONG g_lastNativeHevEndMs = 0;
ULONGLONG g_hevCueCooldown[kHevCueCount] = {};
ULONGLONG g_pendingHevPowerAtMs = 0;
uint32_t g_announcedWeaponsMask = 0;

void ResetHevStateLocked() {
    g_prevBattery = -1;
    g_prevHealth = -1;
    g_lastNativeHevMs = 0;
    g_lastNativeHevEndMs = 0;
    g_pendingHevPowerAtMs = 0;
    g_announcedWeaponsMask = 0;
    for (auto& t : g_hevCueCooldown) t = 0;
}

bool TryTriggerRestoredHev(HevCue cue, const char* sentenceName);

void LogMsg(const char* fmt, ...) {
    if (!g_cfg.debug_log || g_exeDir.empty()) return;
    std::string logPath = g_exeDir + "\\subtitles_debug.log";
    FILE* f = std::fopen(logPath.c_str(), g_logOpened ? "a" : "w");
    if (!f) return;
    g_logOpened = true;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string ParseCmdlineGameDir() {
    const char* cmd = GetCommandLineA();
    if (!cmd) return "valve";
    const char* p = cmd;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (_strnicmp(p, "-game", 5) == 0 && (p[5] == ' ' || p[5] == '\t')) {
            p += 5;
            while (*p == ' ' || *p == '\t') p++;
            bool q = (*p == '"');
            if (q) p++;
            std::string val;
            while (*p && (q ? (*p != '"') : (*p != ' ' && *p != '\t'))) {
                val.push_back(*p++);
            }
            if (!val.empty()) return val;
        }
        if (*p == '"') {
            p++;
            while (*p && *p != '"') p++;
            if (*p == '"') p++;
        } else {
            while (*p && *p != ' ' && *p != '\t') p++;
        }
    }
    return "valve";
}

void ReloadAllFromDisk(bool reloadDb) {
    AcquireSRWLockExclusive(&g_lock);
    std::string fbDir = subs::Database::ReadLiblistFallbackDir(g_exeDir + "\\" + g_activeGameDir);
    if (auto res = g_cfg.LoadIni(g_iniPath, g_activeGameDir, fbDir); !res) {
        LogMsg("[ini] %s", res.error().c_str());
    }
    if (reloadDb) {
        g_wavDurationCache.clear();
        g_loggedMissingSounds.clear();
        g_db.ReloadAll(g_exeDir, g_activeGameDir, g_cfg);
        LogMsg("[reload] gameDir=%s fb=%s hudScale=%.2f compact=%d flash=%d hev=%d restoreHev=%d subs=%zu titles=%zu roles=%zu",
               g_activeGameDir.c_str(), fbDir.c_str(), g_cfg.hud_scale,
               (int)g_cfg.hud_compact_armor, (int)g_cfg.enable_gl_flashlight,
               (int)g_cfg.enable_hev, (int)g_cfg.restore_unused_hev,
               g_db.subtitles.size(), g_db.titles.size(), g_cfg.roles.size());
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void __cdecl Cmd_SubtitlesReload() {
    if (g_engfuncs && g_engfuncs->pfnGetGameDirectory) {
        const char* gd = g_engfuncs->pfnGetGameDirectory();
        if (gd && *gd) g_activeGameDir = gd;
    }
    ReloadAllFromDisk(true);
    if (g_engfuncs && g_engfuncs->Con_Printf) {
        g_engfuncs->Con_Printf("[Subtitles] Reloaded (%s): %u captions, %u titles.\n",
                               g_activeGameDir.c_str(),
                               (unsigned)g_db.subtitles.size(),
                               (unsigned)g_db.titles.size());
    }
}

void CheckIniHotReload() {
    DWORD now = GetTickCount();
    if (now - g_lastIniCheckTick < 1000) return;
    g_lastIniCheckTick = now;

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExA(g_iniPath.c_str(), GetFileExInfoStandard, &fad)) {
        if (CompareFileTime(&fad.ftLastWriteTime, &g_iniMtime) != 0) {
            g_iniMtime = fad.ftLastWriteTime;
            ReloadAllFromDisk(false);
            LogMsg("[cfg] Hot-reloaded subtitles.ini");
        }
    }
}

