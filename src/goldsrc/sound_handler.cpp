#include "goldsrc/sound_handler.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

#include "core/log.hpp"
#include "goldsrc/engine_hooks.hpp"

namespace goldsrc {

namespace {
// GoldSrc sound channels / flags
constexpr int kChanAuto       = 0;
constexpr int kChanStatic     = 6;
constexpr int kSndStop        = 0x20;
constexpr int kSndChangeVol   = 0x40;
constexpr int kSndChangePitch = 0x80;

bool TraceHeightClear(event_api_t* ev, const float* origin, float zOff, int flags, float& outBestFrac) {
    float start[3] = {g_ctx.viewOrg[0], g_ctx.viewOrg[1], g_ctx.viewOrg[2]};
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
} // namespace

uint32_t ComputeCaptionDurationMs(uint32_t rawMs, const std::string& utf8Text) {
    if (rawMs > 0) {
        return std::clamp<uint32_t>(rawMs, 1400, 90000);
    }
    uint32_t chars = 0;
    for (size_t i = 0; i < utf8Text.size(); ) {
        unsigned char c = static_cast<unsigned char>(utf8Text[i]);
        if ((c & 0x80) == 0) i += 1;
        else if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else i += 4;
        chars++;
    }
    return std::clamp<uint32_t>(chars * 50, 300, 4800) + 1000;
}

// Matches GoldSrc hw.dll SND_Spatialize linear attenuation (sound_nominal_clip_dist = 1000.0f).
float ComputePerceivedVolume(float fvol, float dist, float attenuation) noexcept {
    if (fvol <= 0.0f) return 0.0f;
    if (attenuation <= 0.01f || dist <= 0.0f) return fvol;
    float scale = std::clamp(1.0f - dist * (attenuation / 1000.0f), 0.0f, 1.0f);
    return fvol * scale;
}

float ComputeAudibleMaxDist(float fvol, float minVol, float attenuation) noexcept {
    if (attenuation <= 0.01f || fvol <= 0.0f) return 1e9f;
    if (fvol <= minVol) return 0.0f;
    float ratio = std::clamp(minVol / fvol, 0.0f, 1.0f);
    return (1.0f - ratio) * (1000.0f / attenuation);
}

uint32_t ProbeWavDurationMs(const char* rawSfxName) {
    if (!rawSfxName || !*rawSfxName || rawSfxName[0] == '!') return 0;
    if (!g_ctx.engfuncs || !g_ctx.engfuncs->COM_LoadFile || !g_ctx.engfuncs->COM_FreeFile) return 0;

    std::string key = subs::Database::NormalizeKey(rawSfxName);
    if (key.empty()) return 0;

    auto& st = g_ctx.captions;
    {
        AcquireSRWLockShared(&st.lock);
        auto it = st.wavDurationCache.find(key);
        if (it != st.wavDurationCache.end()) {
            uint32_t cached = it->second;
            ReleaseSRWLockShared(&st.lock);
            return cached;
        }
        ReleaseSRWLockShared(&st.lock);
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
    if (uint8_t* buf = g_ctx.engfuncs->COM_LoadFile(vfsPath.c_str(), 5, &len)) {
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
                int adv = 8 + static_cast<int>(chunkSize) + static_cast<int>(chunkSize & 1u);
                if (adv <= 8) break;
                pos += adv;
            }
            if (avgBytesPerSec > 0 && dataBytes > 0) {
                uint64_t ms = (static_cast<uint64_t>(dataBytes) * 1000ULL) / avgBytesPerSec + 350ULL;
                durMs = static_cast<uint32_t>(std::clamp<uint64_t>(ms, 1400ULL, 90000ULL));
            }
        }
        g_ctx.engfuncs->COM_FreeFile(buf);
    }

    AcquireSRWLockExclusive(&st.lock);
    st.wavDurationCache.emplace(std::move(key), durMs);
    ReleaseSRWLockExclusive(&st.lock);
    return durMs;
}

// Returns an audibility multiplier from line-of-sight occlusion:
// 1.0 = clear sightline, 0.70 = behind glass, 0.35 = behind solid wall/corner.
float ComputeOcclusionFactor(const float* origin) {
    if (!g_ctx.engfuncs || !g_ctx.engfuncs->pEventAPI ||
        !g_ctx.engfuncs->pEventAPI->EV_PlayerTrace || !g_ctx.hasViewOrg) {
        return 1.0f;
    }
    event_api_t* ev = g_ctx.engfuncs->pEventAPI;
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

    float factor = 0.35f;
    float bestFrac = 0.0f;
    if (probePass(0x00000001 /* PM_STUDIO_IGNORE */, bestFrac)) {
        factor = 1.0f;
    } else if (probePass(0x00000005 /* PM_STUDIO_IGNORE | PM_GLASS_IGNORE */, bestFrac)) {
        factor = 0.70f;
    }

    if (hasPM) ev->EV_PopPMStates();
    return factor;
}

void HandleSound(int entnum, int entchannel, bool isStatic, sfx_t* sfx, float* origin,
                 float fvol, float attenuation, int flags) {
    if (!g_ctx.clientHooked) {
        if (HMODULE cl = GetModuleHandleA("client.dll")) TryHookClient(cl);
    }
    if (!sfx || reinterpret_cast<uintptr_t>(sfx) < 0x10000) return;

    auto& st = g_ctx.captions;

    const bool exclusiveChan = !isStatic && entnum > 0 &&
                               entchannel != kChanAuto && entchannel != kChanStatic;

    if (flags & kSndStop) {
        std::string stopNorm = sfx->name[0] ? subs::Database::NormalizeKey(sfx->name) : std::string();
        size_t dropped = 0;
        AcquireSRWLockExclusive(&st.lock);
        if (exclusiveChan) {
            dropped = st.DropForChannelLocked(entnum, entchannel, nullptr);
        } else if (!stopNorm.empty()) {
            dropped = st.DropForChannelLocked(entnum, entchannel, &stopNorm);
        }
        ReleaseSRWLockExclusive(&st.lock);
        if (dropped > 0) {
            core::LogMsg("[sfx-stop] ent=%d ch=%d raw='%s' dropped=%zu", entnum, entchannel, sfx->name, dropped);
        }
        return;
    }
    if (flags & (kSndChangeVol | kSndChangePitch)) return;
    if (!sfx->name[0]) return;

    AcquireSRWLockShared(&st.lock);
    std::string norm;
    const subs::SubtitleEntry* entry = st.db.FindSubtitle(sfx->name, norm);
    if (!entry) {
        bool logMiss = st.cfg.debug_log && !norm.empty() &&
                       (sfx->name[0] == '!' || norm.starts_with("scientist/") ||
                        norm.starts_with("barney/") || norm.starts_with("hgrunt/") ||
                        norm.starts_with("fgrunt/") || norm.starts_with("otis/") ||
                        norm.starts_with("rosenberg/") || norm.starts_with("gman/") ||
                        norm.starts_with("fvox/") || norm.starts_with("vox/") ||
                        norm.starts_with("holo/") || norm.starts_with("ops/") ||
                        norm.starts_with("drill/") || norm.starts_with("intro/") ||
                        norm.starts_with("radio/"));
        ReleaseSRWLockShared(&st.lock);
        if (logMiss) {
            AcquireSRWLockExclusive(&st.lock);
            bool first = st.loggedMissingSounds.insert(norm).second;
            ReleaseSRWLockExclusive(&st.lock);
            if (first) {
                core::LogMsg("[sfx-miss] raw='%s' norm='%s'", sfx->name, norm.c_str());
            }
        }
        return;
    }
    auto [color, cat] = st.cfg.Classify(norm, entry->text);
    bool isSfx = core::IsSfxCat(cat);
    bool catEnabled = st.cfg.IsCategoryEnabled(cat);
    float minVol = isSfx ? st.cfg.sfx_min_volume : st.cfg.min_volume;
    bool useAttnCutoff = st.cfg.use_attenuation_cutoff;
    float maxDist = isSfx ? st.cfg.sfx_max_distance : st.cfg.max_distance;
    float noOccMaxDist = st.cfg.no_occlusion_max_distance;
    bool checkOcc = st.cfg.check_occlusion;
    uint32_t durMs = ComputeCaptionDurationMs(entry->duration_ms, entry->text);
    bool autoWavTiming = st.cfg.auto_wav_timing;
    if (isSfx) durMs = std::clamp(durMs, 1100u, 2200u);
    std::wstring wtext = core::Utf8ToWide(entry->text);
    int sfxCooldownMs = st.cfg.sfx_cooldown_ms;
    int maxSfxCaps = st.cfg.max_sfx_captions;
    bool isNoOcc = st.cfg.IsNoOcclusionPrefix(norm);
    ReleaseSRWLockShared(&st.lock);

    if (autoWavTiming && !isSfx) {
        if (uint32_t wavMs = ProbeWavDurationMs(sfx->name); wavMs > 0) {
            durMs = wavMs;
        }
    }

    if (!catEnabled || fvol < minVol || wtext.empty()) {
        core::LogMsg("[sfx-skip] filtered raw='%s' norm='%s' cat=%d vol=%.2f",
                     sfx->name, norm.c_str(), static_cast<int>(cat), fvol);
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
                    originZero || !g_ctx.hasViewOrg || ambientSpecial;

    bool isHgrunt = (norm.starts_with("hg_") || norm.starts_with("hgrunt/") ||
                     norm.starts_with("fg_") || norm.starts_with("fgrunt/") ||
                     norm.starts_with("tg_") || norm.starts_with("mg_") ||
                     norm.starts_with("bo_") || norm.starts_with("ops/"));
    float effMaxDist = isHgrunt ? std::max(maxDist, 1100.0f)
                     : (isNoOcc ? std::max(maxDist, noOccMaxDist) : maxDist);

    float dist = 0.0f;
    float occFactor = 1.0f;
    float perceivedVol = fvol;
    if (!isGlobal && origin && g_ctx.hasViewOrg) {
        float dx = origin[0] - g_ctx.viewOrg[0];
        float dy = origin[1] - g_ctx.viewOrg[1];
        float dz = origin[2] - g_ctx.viewOrg[2];
        dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        bool shouldCheckOcc = checkOcc && !isNoOcc && !(isHgrunt && dist <= 750.0f);
        if (shouldCheckOcc) {
            occFactor = ComputeOcclusionFactor(origin);
        }

        if (useAttnCutoff) {
            // Primary filter: perceived volume (distance attenuation * occlusion factor).
            float ceilingDist = isNoOcc ? std::max(effMaxDist, noOccMaxDist) : std::max(effMaxDist, 1400.0f);
            if (dist > ceilingDist) {
                core::LogMsg("[sfx-skip] too_far raw='%s' norm='%s' dist=%.0f max=%.0f atten=%.2f",
                             sfx->name, norm.c_str(), dist, ceilingDist, attenuation);
                return;
            }
            perceivedVol = ComputePerceivedVolume(fvol, dist, attenuation);
            if (!isNoOcc) {
                perceivedVol *= occFactor;
            }
            if (!isNoOcc && perceivedVol < minVol) {
                core::LogMsg("[sfx-skip] too_quiet raw='%s' norm='%s' dist=%.0f vol=%.2f occ=%.2f pvol=%.2f min=%.2f atten=%.2f",
                             sfx->name, norm.c_str(), dist, fvol, occFactor, perceivedVol, minVol, attenuation);
                return;
            }
        } else {
            if (dist > effMaxDist) {
                core::LogMsg("[sfx-skip] too_far raw='%s' norm='%s' dist=%.0f max=%.0f atten=%.2f",
                             sfx->name, norm.c_str(), dist, effMaxDist, attenuation);
                return;
            }
            if (shouldCheckOcc && occFactor < 0.5f) {
                core::LogMsg("[sfx-skip] occluded raw='%s' norm='%s' dist=%.0f atten=%.2f",
                             sfx->name, norm.c_str(), dist, attenuation);
                return;
            }
        }
    }

    AcquireSRWLockExclusive(&st.lock);
    const ULONGLONG nowMs = st.gameMs;
    if (isSfx) {
        std::string grp = st.SfxGroupKey(norm);
        if (auto it = st.sfxCooldowns.find(grp); it != st.sfxCooldowns.end() &&
            nowMs >= it->second && (nowMs - it->second) < static_cast<ULONGLONG>(sfxCooldownMs)) {
            ReleaseSRWLockExclusive(&st.lock);
            return;
        }
        st.sfxCooldowns[grp] = nowMs;
        int sfxCount = 0;
        for (const auto& c : st.captions) {
            if (core::IsSfxCat(c.cat)) ++sfxCount;
        }
        while (sfxCount >= maxSfxCaps) {
            auto it = std::find_if(st.captions.begin(), st.captions.end(),
                                   [](const core::ActiveCaption& c) { return core::IsSfxCat(c.cat); });
            if (it == st.captions.end()) break;
            st.captions.erase(it);
            --sfxCount;
        }
    } else if (st.lastCapKey == norm && nowMs >= st.lastCapMs && (nowMs - st.lastCapMs) < 1500) {
        ReleaseSRWLockExclusive(&st.lock);
        return;
    } else if (!isGlobal && (norm.starts_with("hg_taunt") || norm.starts_with("hg_alert") ||
                             norm.starts_with("hg_cover") || norm.starts_with("hg_charge") ||
                             norm.starts_with("hg_clear") || norm.starts_with("hg_check") ||
                             norm.starts_with("hg_quest") || norm.starts_with("fg_taunt") ||
                             norm.starts_with("fg_alert") || norm.starts_with("fg_cover") ||
                             norm.starts_with("fg_charge"))) {
        if (auto it = st.sfxCooldowns.find("combat_bark"); it != st.sfxCooldowns.end() &&
            nowMs >= it->second && (nowMs - it->second) < 2200) {
            ReleaseSRWLockExclusive(&st.lock);
            return;
        }
        st.sfxCooldowns["combat_bark"] = nowMs;
    }
    if (exclusiveChan && cat != subs::RoleCat::HEV && !isSfx) {
        st.DropForChannelLocked(entnum, entchannel, nullptr);
    }
    std::erase_if(st.captions, [&](const core::ActiveCaption& c) { return c.key == norm; });
    st.lastCapKey = norm;
    st.lastCapMs = nowMs;
    if (cat == subs::RoleCat::HEV) {
        g_ctx.hev.NoteNativeHevLocked(nowMs, durMs);
    }
    core::ActiveCaption cap;
    cap.text = std::move(wtext);
    cap.color = color;
    cap.cat = cat;
    cap.start_ms = nowMs;
    cap.expire_ms = nowMs + durMs;
    cap.key = norm;
    cap.entnum = entnum;
    cap.channel = entchannel;
    st.AssignSpeakerNumberLocked(cap, nowMs, (origin && !originZero) ? origin : nullptr);
    if (!isGlobal && !isNoOcc && origin && !originZero) {
        cap.hasOrigin = true;
        cap.origin[0] = origin[0];
        cap.origin[1] = origin[1];
        cap.origin[2] = origin[2];
        if (useAttnCutoff && attenuation > 0.01f) {
            float audibleDist = ComputeAudibleMaxDist(fvol * occFactor, minVol, attenuation);
            float ceilingDist = std::max(effMaxDist, 1400.0f);
            cap.fadeDist = std::clamp(audibleDist, 150.0f, ceilingDist) * 1.25f;
        } else {
            cap.fadeDist = effMaxDist * 1.35f;
        }
    }
    st.captions.push_back(std::move(cap));
    while (st.captions.size() > 16) st.captions.pop_front();
    ReleaseSRWLockExclusive(&st.lock);
    core::LogMsg("[sfx-ok] raw='%s' norm='%s' ent=%d ch=%d global=%d dist=%.0f vol=%.2f occ=%.2f pvol=%.2f atten=%.2f dur=%ums",
                 sfx->name, norm.c_str(), entnum, entchannel, static_cast<int>(isGlobal),
                 dist, fvol, occFactor, perceivedVol, attenuation, durMs);
}

void __cdecl Hook_S_StartDynamicSound(int entnum, int entchannel, sfx_t* sfx, float* origin,
                                      float fvol, float attenuation, int flags, int pitch) {
    HandleSound(entnum, entchannel, false, sfx, origin, fvol, attenuation, flags);
    if (g_ctx.origDynSound) g_ctx.origDynSound(entnum, entchannel, sfx, origin, fvol, attenuation, flags, pitch);
}

void __cdecl Hook_S_StartStaticSound(int entnum, int entchannel, sfx_t* sfx, float* origin,
                                     float fvol, float attenuation, int flags, int pitch) {
    HandleSound(entnum, entchannel, true, sfx, origin, fvol, attenuation, flags);
    if (g_ctx.origStatSound) g_ctx.origStatSound(entnum, entchannel, sfx, origin, fvol, attenuation, flags, pitch);
}

client_textmessage_t* __cdecl Hook_TextMessageGet(const char* pName) {
    client_textmessage_t* orig = g_ctx.origTextMsgGet ? g_ctx.origTextMsgGet(pName) : nullptr;
    if (!orig || !pName || !pName[0]) return orig;

    std::string key = subs::Database::UpperTrimTitleKey(pName);

    auto& st = g_ctx.captions;
    AcquireSRWLockExclusive(&st.lock);
    auto it = st.db.titles.find(key);
    if (it == st.db.titles.end()) {
        ReleaseSRWLockExclusive(&st.lock);
        return orig;
    }

    std::wstring wtxt = core::Utf8ToWide(it->second);
    if (wtxt.empty()) {
        ReleaseSRWLockExclusive(&st.lock);
        return orig;
    }

    core::ActiveTitle at;
    at.key = key;
    at.text = std::move(wtxt);
    at.x = orig->x;
    at.y = orig->y;
    at.r = orig->r1;
    at.g = orig->g1;
    at.b = orig->b1;
    at.start_ms = GetTickCount64();

    double fin = std::max(0.2, static_cast<double>(orig->fadein)) * 1000.0;
    double fout = std::max(0.2, static_cast<double>(orig->fadeout)) * 1000.0;
    double hold = std::max(1.5, static_cast<double>(orig->holdtime)) * 1000.0;
    if (orig->effect == 2) {
        double typeMs = static_cast<double>(at.text.size()) * std::max(0.005, static_cast<double>(orig->fadein)) * 1000.0;
        fin = std::clamp(typeMs, 250.0, 2500.0);
    }
    at.fadein_ms = static_cast<ULONGLONG>(fin);
    at.hold_ms = static_cast<ULONGLONG>(hold);
    at.fadeout_ms = static_cast<ULONGLONG>(fout);

    std::erase_if(st.activeTitles, [&](const core::ActiveTitle& existing) {
        if (existing.key == key) return true;
        return std::fabs(existing.x - at.x) < 0.01f &&
               std::fabs(existing.y - at.y) < 0.01f;
    });
    st.activeTitles.push_back(std::move(at));
    if (st.activeTitles.size() > 8) st.activeTitles.erase(st.activeTitles.begin());

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

    ReleaseSRWLockExclusive(&st.lock);
    return &sup;
}

} // namespace goldsrc
