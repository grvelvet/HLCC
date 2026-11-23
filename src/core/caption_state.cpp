#include "core/caption_state.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>

#include "core/log.hpp"

namespace core {

namespace {
bool IsGenericSpeakerTag(std::wstring_view tag) noexcept {
    static constexpr std::wstring_view kGenericTags[] = {
        L"Scientist", L"Security", L"HECU", L"Black Ops", L"Medic", L"Engineer", L"Recruit",
        L"\u0423\u0447\u0451\u043D\u044B\u0439",                                     // Учёный
        L"\u0423\u0447\u0435\u043D\u044B\u0439",                                     // Ученый
        L"\u041E\u0445\u0440\u0430\u043D\u043D\u0438\u043A",                         // Охранник
        L"\u0427\u0451\u0440\u043D\u044B\u0439 \u043E\u0442\u0440\u044F\u0434",     // Чёрный отряд
        L"\u041C\u0435\u0434\u0438\u043A",                                           // Медик
        L"\u0418\u043D\u0436\u0435\u043D\u0435\u0440",                               // Инженер
        L"\u041D\u043E\u0432\u043E\u0431\u0440\u0430\u043D\u0435\u0446",             // Новобранец
    };
    for (auto g : kGenericTags) {
        if (tag == g) return true;
    }
    return false;
}

void ApplySpeakerNumberToText(ActiveCaption& cap, int num) {
    if (num <= 0 || cap.speakerTag.empty()) return;
    if (cap.text.empty() || cap.text[0] != L'[') return;
    size_t rb = cap.text.find(L']');
    if (rb == std::wstring::npos) return;
    std::wstring numbered = L"[" + cap.speakerTag + L" " + std::to_wstring(num) + L"]";
    cap.text.replace(0, rb + 1, numbered);
    cap.speakerNum = num;
    cap.pages.clear();
}
} // namespace

void CaptionState::AssignSpeakerNumberLocked(ActiveCaption& cap, ULONGLONG nowMs, const float* soundOrigin) {
    if (cap.cat != subs::RoleCat::NPC) return;
    if (cap.text.empty() || cap.text[0] != L'[') return;
    size_t rb = cap.text.find(L']');
    if (rb == std::wstring::npos || rb <= 1) return;

    std::wstring tag = cap.text.substr(1, rb - 1);
    if (!IsGenericSpeakerTag(tag)) return;

    cap.isGenericNpc = true;
    cap.speakerTag = tag;
    if (cap.entnum <= 0) return;

    constexpr ULONGLONG kSceneTimeoutMs = 12000ULL;
    constexpr float kSceneMaxDistSq = 500.0f * 500.0f;

    auto& grp = speakerGroups[tag];
    // Drop entities that haven't spoken recently OR are in a different room/scene (> 500u away).
    std::erase_if(grp.entities, [&](const SpeakerEntityTrack& e) {
        if (e.entnum == cap.entnum) return false;
        if (nowMs >= e.lastSeenMs && (nowMs - e.lastSeenMs) > kSceneTimeoutMs) return true;
        if (soundOrigin && e.hasOrigin) {
            float dx = e.origin[0] - soundOrigin[0];
            float dy = e.origin[1] - soundOrigin[1];
            float dz = e.origin[2] - soundOrigin[2];
            if (dx * dx + dy * dy + dz * dz > kSceneMaxDistSq) return true;
        }
        return false;
    });

    auto it = std::find_if(grp.entities.begin(), grp.entities.end(),
                           [&](const SpeakerEntityTrack& e) { return e.entnum == cap.entnum; });
    if (it == grp.entities.end()) {
        SpeakerEntityTrack t;
        t.entnum = cap.entnum;
        t.lastSeenMs = nowMs;
        if (soundOrigin) {
            t.hasOrigin = true;
            t.origin[0] = soundOrigin[0];
            t.origin[1] = soundOrigin[1];
            t.origin[2] = soundOrigin[2];
        }
        grp.entities.push_back(t);
        it = grp.entities.end() - 1;
    } else {
        it->lastSeenMs = nowMs;
        if (soundOrigin) {
            it->hasOrigin = true;
            it->origin[0] = soundOrigin[0];
            it->origin[1] = soundOrigin[1];
            it->origin[2] = soundOrigin[2];
        }
    }

    if (grp.entities.size() < 2) {
        it->assignedNum = 0;
        return;
    }

    auto allocNextNum = [&]() -> int {
        for (int candidate = 1; ; ++candidate) {
            bool used = false;
            for (const auto& e : grp.entities) {
                if (e.assignedNum == candidate) {
                    used = true;
                    break;
                }
            }
            if (!used) return candidate;
        }
    };

    for (auto& e : grp.entities) {
        if (e.assignedNum == 0) {
            e.assignedNum = allocNextNum();
        }
    }

    ApplySpeakerNumberToText(cap, it->assignedNum);
    // Retroactively number any already-visible caption of the same role in this scene.
    for (auto& existing : captions) {
        if (existing.isGenericNpc && existing.speakerNum == 0 &&
            existing.speakerTag == tag && existing.entnum > 0) {
            auto eit = std::find_if(grp.entities.begin(), grp.entities.end(),
                                    [&](const SpeakerEntityTrack& e) { return e.entnum == existing.entnum; });
            if (eit != grp.entities.end() && eit->assignedNum > 0) {
                ApplySpeakerNumberToText(existing, eit->assignedNum);
            }
        }
    }
}

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
    speakerGroups.clear();
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
