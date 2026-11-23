#include "core/caption_state.hpp"

#include <cctype>
#include <vector>

#include "core/log.hpp"

namespace core {

std::string CaptionState::SfxGroupKey(std::string_view norm) const {
    if (size_t sl = norm.find('/'); sl != std::string_view::npos) {
        std::string_view dir = norm.substr(0, sl);
        if (cfg.IsCreatureSfxDir(dir)) return std::string(dir);
    }
    while (!norm.empty() && (std::isdigit(static_cast<unsigned char>(norm.back())) || norm.back() == '_')) {
        norm.remove_suffix(1);
    }
    return std::string(norm);
}

void CaptionState::ClearBuffersLocked() {
    captions.clear();
    activeTitles.clear();
    lastCapKey.clear();
    lastCapMs = 0;
    sfxCooldowns.clear();
}

size_t CaptionState::DropForChannelLocked(int entnum, int channel, const std::string* keyFilter) {
    size_t dropped = std::erase_if(captions, [&](const ActiveCaption& c) {
        if (c.entnum != entnum || c.channel != channel) return false;
        if (!keyFilter && c.cat == subs::RoleCat::HEV) return false;
        return !keyFilter || c.key == *keyFilter;
    });
    if (keyFilter ? (lastCapKey == *keyFilter) : true) {
        lastCapKey.clear();
    }
    return dropped;
}

bool CaptionState::IsGamePausedLocked(ULONGLONG nowRealMs) const noexcept {
    if (lastHudRedrawRealMs == 0 || (nowRealMs - lastHudRedrawRealMs) > 180) return true;
    if (lastTimeAdvanceRealMs == 0 || (nowRealMs - lastTimeAdvanceRealMs) > 180) return true;
    return false;
}

void CaptionState::ReloadAllFromDisk(bool reloadDb) {
    AcquireSRWLockExclusive(&lock);
    std::string fbDir = subs::Database::ReadLiblistFallbackDir(exeDir + "\\" + activeGameDir);
    auto res = cfg.LoadIni(iniPath, activeGameDir, fbDir);
    SetLogContext(cfg.debug_log, exeDir);
    if (!res) {
        LogMsg("[ini] %s", res.error().c_str());
    }
    if (reloadDb) {
        wavDurationCache.clear();
        loggedMissingSounds.clear();
        db.ReloadAll(exeDir, activeGameDir, cfg);
        LogMsg("[reload] gameDir=%s fb=%s hudScale=%.2f compact=%d flash=%d hev=%d restoreHev=%d subs=%zu titles=%zu roles=%zu",
               activeGameDir.c_str(), fbDir.c_str(), cfg.hud_scale,
               static_cast<int>(cfg.hud_compact_armor), static_cast<int>(cfg.enable_gl_flashlight),
               static_cast<int>(cfg.enable_hev), static_cast<int>(cfg.restore_unused_hev),
               db.subtitles.size(), db.titles.size(), cfg.roles.size());
    }
    ReleaseSRWLockExclusive(&lock);
}

void CaptionState::CheckIniHotReload() {
    DWORD now = GetTickCount();
    if (now - lastIniCheckTick < 1000) return;
    lastIniCheckTick = now;

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExA(iniPath.c_str(), GetFileExInfoStandard, &fad)) {
        if (CompareFileTime(&fad.ftLastWriteTime, &iniMtime) != 0) {
            iniMtime = fad.ftLastWriteTime;
            ReloadAllFromDisk(false);
            LogMsg("[cfg] Hot-reloaded subtitles.ini");
        }
    }
}

} // namespace core
