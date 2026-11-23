#pragma once

/**
 * @file sound_handler.hpp
 * @brief Sound event filtering, WAV duration probing, occlusion raycasting, and title interception.
 */

#include <cstdint>
#include <string>

#include "goldsrc/goldsrc.hpp"

namespace goldsrc {

uint32_t ComputeCaptionDurationMs(uint32_t rawMs, const std::string& utf8Text);
float ComputePerceivedVolume(float fvol, float dist, float attenuation) noexcept;
float ComputeAudibleMaxDist(float fvol, float minVol, float attenuation) noexcept;
uint32_t ProbeWavDurationMs(const char* rawSfxName);
bool IsSoundOccluded(const float* origin, float dist);
void HandleSound(int entnum, int entchannel, bool isStatic, sfx_t* sfx, float* origin,
                 float fvol, float attenuation, int flags);

void __cdecl Hook_S_StartDynamicSound(int entnum, int entchannel, sfx_t* sfx, float* origin,
                                      float fvol, float attenuation, int flags, int pitch);
void __cdecl Hook_S_StartStaticSound(int entnum, int entchannel, sfx_t* sfx, float* origin,
                                     float fvol, float attenuation, int flags, int pitch);
client_textmessage_t* __cdecl Hook_TextMessageGet(const char* pName);

} // namespace goldsrc
