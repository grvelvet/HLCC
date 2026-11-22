#pragma once

/**
 * @file sound_handler.hpp
 * @brief Sound event filtering, occlusion probing, anti-spam cooldowns, and HEV restore.
 *
 * Pipeline inside HandleSound():
 *  1. Skip/stop active captions on SND_STOP.
 *  2. Resolve normalized key in g_db (fallback_dir -> active mod override).
 *  3. Classify role/category (NPC, VOX, HEV, SFX, SFX_DANGER).
 *  4. Filter by distance and 2-height line-of-sight trace (with PM_GLASS_IGNORE
 *     within glass_max_distance and bypass for no_occlusion_prefixes).
 *  5. Enforce per-group SFX cooldowns (sfx_cooldown_ms) and squad combat-bark
 *     anti-spam (2200ms) while never displacing active speech.
 */

#include "app_state.hpp"

uint32_t ComputeCaptionDurationMs(uint32_t rawMs, const std::string& utf8Text) {
    if (rawMs > 0) {
        return std::clamp<uint32_t>(rawMs, 1400, 90000);
    }
    uint32_t chars = 0;
    for (size_t i = 0; i < utf8Text.size(); ) {
        unsigned char c = (unsigned char)utf8Text[i];
        if ((c & 0x80) == 0) i += 1;
        else if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else i += 4;
        chars++;
    }
    return std::clamp<uint32_t>(chars * 50, 300, 4800) + 1000;
}

uint32_t ProbeWavDurationMs(const char* rawSfxName) {
    if (!rawSfxName || !*rawSfxName || rawSfxName[0] == '!') return 0;
    if (!g_engfuncs || !g_engfuncs->COM_LoadFile || !g_engfuncs->COM_FreeFile) return 0;

    std::string key = subs::Database::NormalizeKey(rawSfxName);
    if (key.empty()) return 0;

    {
        AcquireSRWLockShared(&g_lock);
        auto it = g_wavDurationCache.find(key);
        if (it != g_wavDurationCache.end()) {
            uint32_t cached = it->second;
            ReleaseSRWLockShared(&g_lock);
            return cached;
        }
        ReleaseSRWLockShared(&g_lock);
    }

    std::string_view rel(rawSfxName);
    while (!rel.empty() && (rel.front() == '*' || rel.front() == '#' || rel.front() == '/' || rel.front() == '\\')) {
        rel.remove_prefix(1);
    }
    std::string vfsPath = "sound/";
    if (rel.size() >= 6 && _strnicmp(rel.data(), "sound/", 6) == 0) {
        vfsPath.assign(rel);
    } else if (rel.size() >= 6 && _strnicmp(rel.data(), "sound\\", 6) == 0) {
        vfsPath.assign(rel);
    } else {
        vfsPath.append(rel);
    }
    for (char& ch : vfsPath) {
        if (ch == '\\') ch = '/';
    }

    uint32_t durMs = 0;
    int len = 0;
    if (uint8_t* buf = g_engfuncs->COM_LoadFile(vfsPath.c_str(), 5, &len)) {
        if (len >= 44 && std::memcmp(buf, "RIFF", 4) == 0 && std::memcmp(buf + 8, "WAVE", 4) == 0) {
            uint32_t avgBytesPerSec = 0;
            uint32_t dataBytes = 0;
            int pos = 12;
            while (pos + 8 <= len) {
                uint32_t chunkSize = 0;
                std::memcpy(&chunkSize, buf + pos + 4, 4);
                if (std::memcmp(buf + pos, "fmt ", 4) == 0 && chunkSize >= 12 && pos + 8 + 12 <= len) {
                    std::memcpy(&avgBytesPerSec, buf + pos + 8 + 8, 4);
                } else if (std::memcmp(buf + pos, "data", 4) == 0) {
                    uint32_t avail = static_cast<uint32_t>(len - (pos + 8));
                    dataBytes = std::min(chunkSize, avail);
                }
                if (chunkSize > static_cast<uint32_t>(len - (pos + 8))) break;
                int adv = 8 + static_cast<int>(chunkSize) + (chunkSize & 1u);
                if (adv <= 8) break;
                pos += adv;
            }
            if (avgBytesPerSec > 0 && dataBytes > 0) {
                uint64_t ms = (static_cast<uint64_t>(dataBytes) * 1000ULL) / avgBytesPerSec + 350ULL;
                durMs = static_cast<uint32_t>(std::clamp<uint64_t>(ms, 1400ULL, 90000ULL));
            }
        }
        g_engfuncs->COM_FreeFile(buf);
    }

    AcquireSRWLockExclusive(&g_lock);
    g_wavDurationCache.emplace(std::move(key), durMs);
    ReleaseSRWLockExclusive(&g_lock);
    return durMs;
}

bool TraceHeightClear(event_api_t* ev, const float* origin, float zOff, int flags, float& outBestFrac) {
    float start[3] = {g_viewOrg[0], g_viewOrg[1], g_viewOrg[2]};
    float end[3]   = {origin[0], origin[1], origin[2] + zOff};
    float dx = end[0] - start[0];
    float dy = end[1] - start[1];
    float dz = end[2] - start[2];
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len <= 24.0f) {
        outBestFrac = 1.0f;
        return true;
    }

    pmtrace_t tr{};
    ev->EV_PlayerTrace(start, end, flags, -1, &tr);
    if (tr.startsolid && !tr.allsolid) {
        float scale = 22.0f / len;
        float nudged[3] = {start[0] + dx * scale, start[1] + dy * scale, start[2] + dz * scale};
        tr = pmtrace_t{};
        ev->EV_PlayerTrace(nudged, end, flags, -1, &tr);
    }
    if (tr.fraction > outBestFrac) outBestFrac = tr.fraction;
    return tr.fraction >= 0.92f;
}

bool IsSoundOccluded(const float* origin, float dist) {
    if (!g_engfuncs || !g_engfuncs->pEventAPI || !g_engfuncs->pEventAPI->EV_PlayerTrace || !g_hasViewOrg) {
        return false;
    }
    event_api_t* ev = g_engfuncs->pEventAPI;
    bool hasPM = (ev->EV_PushPMStates && ev->EV_PopPMStates && ev->EV_SetTraceHull);
    if (hasPM) {
        ev->EV_PushPMStates();
        ev->EV_SetTraceHull(2);
    }

    auto probePass = [&](int flags, float& bestFrac) -> bool {
        if (TraceHeightClear(ev, origin, 48.0f, flags, bestFrac)) return true;
        if (TraceHeightClear(ev, origin, 16.0f, flags, bestFrac)) return true;
        return false;
    };

    float bestFrac = 0.0f;
    bool clear = probePass(0x00000001 /* PM_STUDIO_IGNORE */, bestFrac);
    if (!clear && dist <= g_cfg.glass_max_distance) {
        clear = probePass(0x00000005 /* PM_STUDIO_IGNORE | PM_GLASS_IGNORE */, bestFrac);
    }

    if (hasPM) ev->EV_PopPMStates();
    return !clear;
}

void TryHookClient(HMODULE cl);

void HandleSound(int entnum, int entchannel, bool isStatic, sfx_t* sfx, float* origin, float fvol, float attenuation, int flags) {
    if (!g_clientHooked) {
        if (HMODULE cl = GetModuleHandleA("client.dll")) TryHookClient(cl);
    }
    if (!sfx || reinterpret_cast<uintptr_t>(sfx) < 0x10000) return;

    // A dynamic sound on a real entity + exclusive channel replaces whatever
    // that entity was playing on that channel (engine behaviour).
    const bool exclusiveChan = !isStatic && entnum > 0 &&
                               entchannel != kChanAuto && entchannel != kChanStatic;

    if (flags & kSndStop) {
        std::string stopNorm = sfx->name[0] ? subs::Database::NormalizeKey(sfx->name) : std::string();
        size_t dropped = 0;
        AcquireSRWLockExclusive(&g_lock);
        if (exclusiveChan) {
            dropped = DropCaptionsForChannelLocked(entnum, entchannel, nullptr);
        } else if (!stopNorm.empty()) {
            dropped = DropCaptionsForChannelLocked(entnum, entchannel, &stopNorm);
        }
        ReleaseSRWLockExclusive(&g_lock);
        if (dropped > 0) {
            LogMsg("[sfx-stop] ent=%d ch=%d raw='%s' dropped=%zu", entnum, entchannel, sfx->name, dropped);
        }
        return;
    }
    if (flags & (kSndChangeVol | kSndChangePitch)) return;
    if (!sfx->name[0]) return;

    AcquireSRWLockShared(&g_lock);
    std::string norm;
    const subs::SubtitleEntry* entry = g_db.FindSubtitle(sfx->name, norm);
    if (!entry) {
        ReleaseSRWLockShared(&g_lock);
        return;
    }
    auto [color, cat] = g_cfg.Classify(norm, entry->text);
    bool isSfx = IsSfxCat(cat);
    bool catEnabled = g_cfg.IsCategoryEnabled(cat);
    float minVol = isSfx ? g_cfg.sfx_min_volume : g_cfg.min_volume;
    float maxDist = isSfx ? g_cfg.sfx_max_distance : g_cfg.max_distance;
    bool checkOcc = g_cfg.check_occlusion;
    uint32_t durMs = ComputeCaptionDurationMs(entry->duration_ms, entry->text);
    bool autoWavTiming = g_cfg.auto_wav_timing;
    if (isSfx) durMs = std::clamp(durMs, 1100u, 2200u);
    std::wstring wtext = Utf8ToWide(entry->text);
    int sfxCooldownMs = g_cfg.sfx_cooldown_ms;
    int maxSfxCaps = g_cfg.max_sfx_captions;
    ReleaseSRWLockShared(&g_lock);

    if (autoWavTiming && !isSfx) {
        if (uint32_t wavMs = ProbeWavDurationMs(sfx->name); wavMs > 0) {
            durMs = wavMs;
        }
    }

    if (!catEnabled || (g_isAddonWithoutHev && cat == subs::RoleCat::HEV) || fvol < minVol || wtext.empty()) {
        LogMsg("[sfx-skip] filtered raw='%s' norm='%s' cat=%d vol=%.2f", sfx->name, norm.c_str(), (int)cat, fvol);
        return;
    }

    bool originZero = (!origin || (origin[0] == 0.0f && origin[1] == 0.0f && origin[2] == 0.0f));
    bool ambientSpecial = (norm.starts_with("tride/") ||
                           norm.starts_with("nihilanth/") ||
                           norm.starts_with("sc_dis") ||
                           norm.starts_with("scientist/c1a0_sci_dis") ||
                           norm.starts_with("intro/") ||
                           norm.starts_with("intro_") ||
                           norm.starts_with("fgrunt/intro_") ||
                           norm.starts_with("misc/broken_tram") ||
                           norm.starts_with("radio/") ||
                           norm.starts_with("rd_") ||
                           norm.starts_with("of7a0_gm") ||
                           norm.starts_with("gman/of7a0_gm"));
    bool isGlobal = (cat == subs::RoleCat::HEV) ||
                    (cat == subs::RoleCat::VOX && (fvol >= 0.8f || attenuation <= 0.8f)) ||
                    (attenuation <= 0.01f) ||
                    originZero || !g_hasViewOrg || ambientSpecial;

    bool isHgrunt = (norm.starts_with("hg_") || norm.starts_with("hgrunt/") ||
                     norm.starts_with("fg_") || norm.starts_with("fgrunt/") ||
                     norm.starts_with("tg_") || norm.starts_with("mg_") ||
                     norm.starts_with("bo_") || norm.starts_with("ops/"));
    bool isNoOcc = g_cfg.IsNoOcclusionPrefix(norm);
    float effMaxDist = isHgrunt ? std::max(maxDist, 1100.0f)
                     : (isNoOcc ? std::max(maxDist, g_isBshiftGame ? 2000.0f : 1500.0f) : maxDist);

    float dist = 0.0f;
    if (!isGlobal && origin && g_hasViewOrg) {
        float dx = origin[0] - g_viewOrg[0];
        float dy = origin[1] - g_viewOrg[1];
        float dz = origin[2] - g_viewOrg[2];
        dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist > effMaxDist) {
            LogMsg("[sfx-skip] too_far raw='%s' norm='%s' dist=%.0f max=%.0f atten=%.2f", sfx->name, norm.c_str(), dist, effMaxDist, attenuation);
            return;
        }
        bool shouldCheckOcc = checkOcc && !isNoOcc && !(isHgrunt && dist <= 750.0f);
        if (shouldCheckOcc && IsSoundOccluded(origin, dist)) {
            LogMsg("[sfx-skip] occluded raw='%s' norm='%s' dist=%.0f atten=%.2f", sfx->name, norm.c_str(), dist, attenuation);
            return;
        }
    }

    AcquireSRWLockExclusive(&g_lock);
    const ULONGLONG nowMs = g_gameMs;
    if (isSfx) {
        std::string grp = SfxGroupKey(norm);
        if (auto it = g_sfxCooldowns.find(grp); it != g_sfxCooldowns.end() &&
            nowMs >= it->second && (nowMs - it->second) < (ULONGLONG)sfxCooldownMs) {
            ReleaseSRWLockExclusive(&g_lock);
            return;
        }
        g_sfxCooldowns[grp] = nowMs;
        // Limit concurrent SFX captions without ever displacing active speech
        int sfxCount = 0;
        for (const auto& c : g_captions) {
            if (IsSfxCat(c.cat)) ++sfxCount;
        }
        while (sfxCount >= maxSfxCaps) {
            auto it = std::find_if(g_captions.begin(), g_captions.end(),
                                   [](const ActiveCaption& c) { return IsSfxCat(c.cat); });
            if (it == g_captions.end()) break;
            g_captions.erase(it);
            --sfxCount;
        }
    } else if (g_lastCapKey == norm && nowMs >= g_lastCapMs && (nowMs - g_lastCapMs) < 1500) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    } else if (!isGlobal && (norm.starts_with("hg_taunt") || norm.starts_with("hg_alert") ||
                             norm.starts_with("hg_cover") || norm.starts_with("hg_charge") ||
                             norm.starts_with("hg_clear") || norm.starts_with("hg_check") ||
                             norm.starts_with("hg_quest") || norm.starts_with("fg_taunt") ||
                             norm.starts_with("fg_alert") || norm.starts_with("fg_cover") ||
                             norm.starts_with("fg_charge"))) {
        if (auto it = g_sfxCooldowns.find("combat_bark"); it != g_sfxCooldowns.end() &&
            nowMs >= it->second && (nowMs - it->second) < 2200) {
            ReleaseSRWLockExclusive(&g_lock);
            return;
        }
        g_sfxCooldowns["combat_bark"] = nowMs;
    }
    if (exclusiveChan && cat != subs::RoleCat::HEV && !isSfx) {
        DropCaptionsForChannelLocked(entnum, entchannel, nullptr);
    }
    // The same line re-triggered elsewhere: refresh instead of stacking a duplicate.
    std::erase_if(g_captions, [&](const ActiveCaption& c) { return c.key == norm; });
    g_lastCapKey = norm;
    g_lastCapMs = nowMs;
    if (cat == subs::RoleCat::HEV) {
        g_lastNativeHevMs = nowMs;
        g_lastNativeHevEndMs = std::max(g_lastNativeHevEndMs, nowMs + (ULONGLONG)durMs);
    }
    ActiveCaption cap;
    cap.text = std::move(wtext);
    cap.color = color;
    cap.cat = cat;
    cap.start_ms = nowMs;
    cap.expire_ms = nowMs + durMs;
    cap.key = norm;
    cap.entnum = entnum;
    cap.channel = entchannel;
    g_captions.push_back(std::move(cap));
    while (g_captions.size() > 16) g_captions.pop_front();
    ReleaseSRWLockExclusive(&g_lock);
    LogMsg("[sfx-ok] raw='%s' norm='%s' ent=%d ch=%d global=%d dist=%.0f vol=%.2f atten=%.2f dur=%ums",
           sfx->name, norm.c_str(), entnum, entchannel, (int)isGlobal, dist, fvol, attenuation, durMs);
}

void __cdecl Hook_S_StartDynamicSound(int entnum, int entchannel, sfx_t* sfx, float* origin, float fvol, float attenuation, int flags, int pitch) {
    HandleSound(entnum, entchannel, false, sfx, origin, fvol, attenuation, flags);
    if (g_origDynSound) g_origDynSound(entnum, entchannel, sfx, origin, fvol, attenuation, flags, pitch);
}

void __cdecl Hook_S_StartStaticSound(int entnum, int entchannel, sfx_t* sfx, float* origin, float fvol, float attenuation, int flags, int pitch) {
    HandleSound(entnum, entchannel, true, sfx, origin, fvol, attenuation, flags);
    if (g_origStatSound) g_origStatSound(entnum, entchannel, sfx, origin, fvol, attenuation, flags, pitch);
}

client_textmessage_t* __cdecl Hook_TextMessageGet(const char* pName) {
    client_textmessage_t* orig = g_origTextMsgGet ? g_origTextMsgGet(pName) : nullptr;
    if (!orig || !pName || !pName[0]) return orig;

    std::string key = subs::Database::UpperTrimTitleKey(pName);

    AcquireSRWLockExclusive(&g_lock);
    auto it = g_db.titles.find(key);
    if (it == g_db.titles.end()) {
        ReleaseSRWLockExclusive(&g_lock);
        return orig;
    }

    std::wstring wtxt = Utf8ToWide(it->second);
    if (wtxt.empty()) {
        ReleaseSRWLockExclusive(&g_lock);
        return orig;
    }

    ActiveTitle at;
    at.key = key;
    at.text = std::move(wtxt);
    at.x = orig->x;
    at.y = orig->y;
    at.r = orig->r1;
    at.g = orig->g1;
    at.b = orig->b1;
    at.start_ms = GetTickCount64();

    double fin = std::max(0.2, (double)orig->fadein) * 1000.0;
    double fout = std::max(0.2, (double)orig->fadeout) * 1000.0;
    double hold = std::max(1.5, (double)orig->holdtime) * 1000.0;
    if (orig->effect == 2) {
        double typeMs = (double)at.text.size() * std::max(0.005, (double)orig->fadein) * 1000.0;
        fin = std::clamp(typeMs, 250.0, 2500.0);
    }
    at.fadein_ms = (ULONGLONG)fin;
    at.hold_ms = (ULONGLONG)hold;
    at.fadeout_ms = (ULONGLONG)fout;

    // Remove any existing title with the same key OR at the same (x, y) screen slot
    // (matches CHudMessage::MessageAdd and prevents overlapping titles in Uplink / c0a0).
    std::erase_if(g_activeTitles, [&](const ActiveTitle& existing) {
        if (existing.key == key) return true;
        return std::fabs(existing.x - at.x) < 0.01f &&
               std::fabs(existing.y - at.y) < 0.01f;
    });
    g_activeTitles.push_back(std::move(at));
    if (g_activeTitles.size() > 8) g_activeTitles.erase(g_activeTitles.begin());

    // Return a silent copy so the vanilla engine doesn't draw garbled ASCII over our VGUI title
    static client_textmessage_t s_suppressed[16];
    static unsigned s_suppressedIdx = 0;
    client_textmessage_t& sup = s_suppressed[s_suppressedIdx++ & 15];
    sup = *orig;
    sup.effect = 0;
    sup.r1 = sup.g1 = sup.b1 = sup.a1 = 0;
    sup.r2 = sup.g2 = sup.b2 = sup.a2 = 0;
    sup.fadein = 0.0f;
    sup.fadeout = 0.0f;
    sup.holdtime = 0.0f;
    sup.fxtime = 0.0f;
    sup.pName = "";
    sup.pMessage = "";

    ReleaseSRWLockExclusive(&g_lock);
    return &sup;
}

void __cdecl Cmd_SubtitlesClear() {
    AcquireSRWLockExclusive(&g_lock);
    ClearCaptionBuffersLocked();
    ReleaseSRWLockExclusive(&g_lock);
}

void PlayHevSentence(const char* sentence) {
    if (!g_engfuncs || !sentence || !*sentence) return;
    if (g_engfuncs->pfnClientCmd) {
        char cmd[128];
        std::snprintf(cmd, sizeof(cmd), "speak \"%s\"\n", sentence);
        g_engfuncs->pfnClientCmd(cmd);
    } else if (g_engfuncs->pfnPlaySoundByName) {
        g_engfuncs->pfnPlaySoundByName(sentence, 0.85f);
    }
}

bool TryTriggerRestoredHev(HevCue cue, const char* sentenceName) {
    AcquireSRWLockExclusive(&g_lock);
    const ULONGLONG nowMs = g_gameMs;
    const ULONGLONG nativeGapMs = (cue == kHevPowerRestored) ? 3500ULL : 1800ULL;
    const ULONGLONG cueCooldownMs = (cue == kHevPowerRestored) ? 45000ULL : 15000ULL;
    bool ok = g_cfg.restore_unused_hev && !g_isMModGame && !g_isAddonWithoutHev && g_cfg.enable_hev && g_hevVoiceEnabled &&
              nowMs >= 2000 &&
              (g_lastNativeHevMs == 0 || nowMs - g_lastNativeHevMs >= nativeGapMs) &&
              (g_lastNativeHevEndMs == 0 || nowMs >= g_lastNativeHevEndMs + nativeGapMs) &&
              (g_hevCueCooldown[cue] == 0 || nowMs - g_hevCueCooldown[cue] >= cueCooldownMs);
    if (ok) {
        g_hevCueCooldown[cue] = nowMs;
        g_lastNativeHevMs = nowMs;
        g_lastNativeHevEndMs = std::max(g_lastNativeHevEndMs, nowMs + 3500ULL);
    }
    ReleaseSRWLockExclusive(&g_lock);

    if (ok) {
        PlayHevSentence(sentenceName);
        LogMsg("[hev-restore] played %s", sentenceName);
    }
    return ok;
}

void __cdecl Cmd_HevVoice() {
    g_hevVoiceEnabled = !g_hevVoiceEnabled;
    PlayHevSentence(g_hevVoiceEnabled ? "!HEV_V0" : "!HEV_V1");
}

void OnBatteryValue(int battery) {
    int prev = g_prevBattery;
    g_prevBattery = battery;
    if (prev < 0) return;
    if (prev > 0 && battery == 0) {
        AcquireSRWLockExclusive(&g_lock);
        g_pendingHevPowerAtMs = 0;
        ReleaseSRWLockExclusive(&g_lock);
        TryTriggerRestoredHev(kHevArmorGone, (prev % 2 == 0) ? "!HEV_E0" : "!HEV_E1");
    } else if (prev == 0 && battery > 0) {
        AcquireSRWLockExclusive(&g_lock);
        g_pendingHevPowerAtMs = g_gameMs + 2000ULL;
        ReleaseSRWLockExclusive(&g_lock);
    }
}

void OnHealthValue(int health) {
    int prev = g_prevHealth;
    g_prevHealth = health;
    if (prev < 0) return;
    if (prev > 15 && health > 0 && health <= 15) {
        TryTriggerRestoredHev(kHevSeekMedic, "!HEV_DMG7");
    }
}

void OnDamageEvent(int dmgSave, int dmgTake, uint32_t dmgBits) {
    if (dmgSave + dmgTake <= 0) return;
    constexpr uint32_t kDmgBurn     = (1u << 3) | (1u << 21);
    constexpr uint32_t kDmgFall     = (1u << 5);
    constexpr uint32_t kDmgBlast    = (1u << 6);
    constexpr uint32_t kDmgShock    = (1u << 8);
    constexpr uint32_t kDmgPhysical = (1u << 1) | (1u << 2) | (1u << 7);

    if (dmgBits & kDmgShock) {
        TryTriggerRestoredHev(kHevShock, "!HEV_SHOCK");
    } else if (dmgBits & kDmgBurn) {
        TryTriggerRestoredHev(kHevFire, "!HEV_FIRE");
    } else if ((dmgBits & kDmgFall) && dmgTake >= 10) {
        TryTriggerRestoredHev(kHevFracture, (dmgTake >= 25) ? "!HEV_DMG5" : "!HEV_DMG4");
    } else if ((dmgBits & kDmgBlast) && dmgTake >= 25) {
        TryTriggerRestoredHev(kHevBloodLoss, "!HEV_DMG6");
    } else if ((dmgBits & kDmgPhysical) && dmgTake >= 20) {
        TryTriggerRestoredHev(kHevLaceration, "!HEV_DMG1");
    } else if (dmgSave >= 20 && g_prevBattery > 0) {
        TryTriggerRestoredHev((g_prevBattery <= 10) ? kHevSuitCrit : kHevSuitDmg,
                              (g_prevBattery <= 10) ? "!HEV_E2" : "!HEV_E4");
    }
}

