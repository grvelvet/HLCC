#pragma once

#include <windows.h>

#include <cstdint>

#include "core/caption_state.hpp"
#include "goldsrc/goldsrc.hpp"

namespace goldsrc {

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

struct HevRestore {
    bool voiceEnabled = true;
    int prevBattery = -1;
    int prevHealth = -1;
    ULONGLONG lastNativeHevMs = 0;
    ULONGLONG lastNativeHevEndMs = 0;
    ULONGLONG cueCooldown[kHevCueCount] = {};
    ULONGLONG pendingPowerAtMs = 0;
    uint32_t announcedWeaponsMask = 0;

    void ResetLocked() noexcept;
    void NoteNativeHevLocked(ULONGLONG nowMs, uint32_t durMs) noexcept;
    bool ConsumePendingPowerRestoredLocked(ULONGLONG nowMs) noexcept;

    static void PlaySentence(cl_enginefunc_t* eng, const char* sentence);
    bool TryTrigger(core::CaptionState& state, cl_enginefunc_t* eng, HevCue cue, const char* sentenceName);
    void ToggleVoice(cl_enginefunc_t* eng);
    void OnBatteryValue(core::CaptionState& state, cl_enginefunc_t* eng, int battery);
    void OnHealthValue(core::CaptionState& state, cl_enginefunc_t* eng, int health);
    void OnDamageEvent(core::CaptionState& state, cl_enginefunc_t* eng, int dmgSave, int dmgTake, uint32_t dmgBits);
    void OnWeapPickup(core::CaptionState& state, cl_enginefunc_t* eng, int weapId);
};

} // namespace goldsrc
