#include "goldsrc/hev_restore.hpp"

#include <algorithm>
#include <cstdio>

#include "core/log.hpp"

namespace goldsrc {

namespace {
const char* WeaponIdToHevSentence(int id) {
    switch (id) {
        case 2:  return "!HEV_PISTOL";
        case 3:  return "!HEV_44PISTOL";
        case 4:  return "!HEV_ASSAULT";
        case 6:  return "!HEV_XBOW";
        case 7:  return "!HEV_SHOTGUN";
        case 8:  return "!HEV_RPG";
        case 9:  return "!HEV_GAUSS";
        case 10: return "!HEV_EGON";
        case 11: return "!HEV_HORNET";
        case 12: return "!HEV_GRENADE";
        case 13: return "!HEV_TRIPMINE";
        case 14: return "!HEV_SATCHEL";
        case 15: return "!HEV_SQUEEK";
        default: return nullptr;
    }
}
} // namespace

void HevRestore::ResetLocked() noexcept {
    prevBattery = -1;
    prevHealth = -1;
    lastNativeHevMs = 0;
    lastNativeHevEndMs = 0;
    pendingPowerAtMs = 0;
    announcedWeaponsMask = 0;
    for (auto& t : cueCooldown) t = 0;
}

void HevRestore::NoteNativeHevLocked(ULONGLONG nowMs, uint32_t durMs) noexcept {
    lastNativeHevMs = nowMs;
    lastNativeHevEndMs = std::max(lastNativeHevEndMs, nowMs + static_cast<ULONGLONG>(durMs));
}

bool HevRestore::ConsumePendingPowerRestoredLocked(ULONGLONG nowMs) noexcept {
    if (pendingPowerAtMs != 0 && nowMs >= pendingPowerAtMs) {
        pendingPowerAtMs = 0;
        return (prevBattery > 0);
    }
    return false;
}

void HevRestore::PlaySentence(cl_enginefunc_t* eng, const char* sentence) {
    if (!eng || !sentence || !*sentence) return;
    if (eng->pfnClientCmd) {
        char cmd[128];
        std::snprintf(cmd, sizeof(cmd), "speak \"%s\"\n", sentence);
        eng->pfnClientCmd(cmd);
    } else if (eng->pfnPlaySoundByName) {
        eng->pfnPlaySoundByName(sentence, 0.85f);
    }
}

bool HevRestore::TryTrigger(core::CaptionState& state, cl_enginefunc_t* eng, HevCue cue, const char* sentenceName) {
    AcquireSRWLockExclusive(&state.lock);
    const ULONGLONG nowMs = state.gameMs;
    const ULONGLONG nativeGapMs = (cue == kHevPowerRestored) ? 3500ULL : 1800ULL;
    const ULONGLONG cueCooldownMs = (cue == kHevPowerRestored) ? 45000ULL : 15000ULL;
    bool ok = state.cfg.restore_unused_hev && state.cfg.enable_hev && voiceEnabled &&
              nowMs >= 2000 &&
              (lastNativeHevMs == 0 || nowMs - lastNativeHevMs >= nativeGapMs) &&
              (lastNativeHevEndMs == 0 || nowMs >= lastNativeHevEndMs + nativeGapMs) &&
              (cueCooldown[cue] == 0 || nowMs - cueCooldown[cue] >= cueCooldownMs);
    if (ok) {
        cueCooldown[cue] = nowMs;
        lastNativeHevMs = nowMs;
        lastNativeHevEndMs = std::max(lastNativeHevEndMs, nowMs + 3500ULL);
    }
    ReleaseSRWLockExclusive(&state.lock);

    if (ok) {
        PlaySentence(eng, sentenceName);
        core::LogMsg("[hev-restore] played %s", sentenceName);
    }
    return ok;
}

void HevRestore::ToggleVoice(cl_enginefunc_t* eng) {
    voiceEnabled = !voiceEnabled;
    PlaySentence(eng, voiceEnabled ? "!HEV_V0" : "!HEV_V1");
}

void HevRestore::OnBatteryValue(core::CaptionState& state, cl_enginefunc_t* eng, int battery) {
    int prev = prevBattery;
    prevBattery = battery;
    if (prev < 0) return;
    if (prev > 0 && battery == 0) {
        AcquireSRWLockExclusive(&state.lock);
        pendingPowerAtMs = 0;
        ReleaseSRWLockExclusive(&state.lock);
        TryTrigger(state, eng, kHevArmorGone, (prev % 2 == 0) ? "!HEV_E0" : "!HEV_E1");
    } else if (prev == 0 && battery > 0) {
        AcquireSRWLockExclusive(&state.lock);
        pendingPowerAtMs = state.gameMs + 2000ULL;
        ReleaseSRWLockExclusive(&state.lock);
    }
}

void HevRestore::OnHealthValue(core::CaptionState& state, cl_enginefunc_t* eng, int health) {
    int prev = prevHealth;
    prevHealth = health;
    if (prev < 0) return;
    if (prev > 15 && health > 0 && health <= 15) {
        TryTrigger(state, eng, kHevSeekMedic, "!HEV_DMG7");
    }
}

void HevRestore::OnDamageEvent(core::CaptionState& state, cl_enginefunc_t* eng, int dmgSave, int dmgTake, uint32_t dmgBits) {
    if (dmgSave + dmgTake <= 0) return;
    constexpr uint32_t kDmgBurn     = (1u << 3) | (1u << 21);
    constexpr uint32_t kDmgFall     = (1u << 5);
    constexpr uint32_t kDmgBlast    = (1u << 6);
    constexpr uint32_t kDmgShock    = (1u << 8);
    constexpr uint32_t kDmgPhysical = (1u << 1) | (1u << 2) | (1u << 7);

    if (dmgBits & kDmgShock) {
        TryTrigger(state, eng, kHevShock, "!HEV_SHOCK");
    } else if (dmgBits & kDmgBurn) {
        TryTrigger(state, eng, kHevFire, "!HEV_FIRE");
    } else if ((dmgBits & kDmgFall) && dmgTake >= 10) {
        TryTrigger(state, eng, kHevFracture, (dmgTake >= 25) ? "!HEV_DMG5" : "!HEV_DMG4");
    } else if ((dmgBits & kDmgBlast) && dmgTake >= 25) {
        TryTrigger(state, eng, kHevBloodLoss, "!HEV_DMG6");
    } else if ((dmgBits & kDmgPhysical) && dmgTake >= 20) {
        TryTrigger(state, eng, kHevLaceration, "!HEV_DMG1");
    } else if (dmgSave >= 20 && prevBattery > 0) {
        TryTrigger(state, eng, (prevBattery <= 10) ? kHevSuitCrit : kHevSuitDmg,
                   (prevBattery <= 10) ? "!HEV_E2" : "!HEV_E4");
    }
}

void HevRestore::OnWeapPickup(core::CaptionState& state, cl_enginefunc_t* eng, int weapId) {
    if (weapId < 0 || weapId >= 32) return;
    const char* sentence = WeaponIdToHevSentence(weapId);
    if (!sentence) return;

    AcquireSRWLockExclusive(&state.lock);
    const ULONGLONG nowMs = state.gameMs;
    uint32_t bit = (1u << weapId);
    bool ok = state.cfg.restore_unused_hev && state.cfg.enable_hev && voiceEnabled &&
              nowMs >= 1000 && (announcedWeaponsMask & bit) == 0 &&
              (lastNativeHevEndMs == 0 || nowMs >= lastNativeHevEndMs + 1000ULL);
    if (ok) {
        announcedWeaponsMask |= bit;
        lastNativeHevMs = nowMs;
        lastNativeHevEndMs = std::max(lastNativeHevEndMs, nowMs + 2500ULL);
    }
    ReleaseSRWLockExclusive(&state.lock);

    if (ok) {
        PlaySentence(eng, sentence);
        core::LogMsg("[hev-restore] weapon=%d -> %s", weapId, sentence);
    }
}

} // namespace goldsrc
