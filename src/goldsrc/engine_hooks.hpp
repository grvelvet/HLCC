#pragma once

/**
 * @file engine_hooks.hpp
 * @brief Central GoldSrc hook state and engine lifecycle entry points.
 *
 * Owns the single shared EngineContext instance across all translation units
 * (avoiding anonymous-namespace duplication) and bridges hw.dll / client.dll /
 * opengl32.dll callbacks to core/ and render/.
 */

#include <windows.h>

#include <unordered_set>

#include "core/caption_state.hpp"
#include "goldsrc/goldsrc.hpp"
#include "goldsrc/hev_restore.hpp"
#include "render/flashlight_gl.hpp"
#include "render/gl_font.hpp"
#include "render/hud_fbo_scaler.hpp"
#include "render/overlay_render.hpp"

namespace goldsrc {

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
using FnAllocDlight    = dlight_t*(__cdecl*)(int key);
using FnSprLoad        = int(__cdecl*)(const char*);
using FnSprSet         = void(__cdecl*)(int, int, int, int);
using FnSprDraw        = void(__cdecl*)(int, int, int, const void*);
using FnFillRgba       = void(__cdecl*)(int, int, int, int, int, int, int, int);
using FnDrawChar       = int(__cdecl*)(int, int, int, int, int, int);
using FnTriBegin       = void(__cdecl*)(int);
using FnTriEnd         = void(__cdecl*)(void);

struct EngineContext {
    core::CaptionState captions;
    render::OverlayRenderer overlay;
    hud_fbo::HudFboScaler hudFbo;
    gl_flash::GlFlashlight glFlashlight;
    HevRestore hev;
    glfont::GlApi gl;

    cl_enginefunc_t* engfuncs = nullptr;
    int screenW = 0;
    int screenH = 0;
    float viewOrg[3] = {0.0f, 0.0f, 0.0f};
    float viewAngles[3] = {0.0f, 0.0f, 0.0f};
    bool hasViewOrg = false;
    bool resetRequested = false;

    bool hwHooked = false;
    bool clientHooked = false;
    bool glHooked = false;
    bool hudFboCapturedThisFrame = false;

    int prevHealthMaxX = 0;
    int curHealthMaxX = 0;
    int prevArmorMinX = 100000;
    int curArmorMinX = 100000;

    bool flashlightMsgOn = false;
    ULONGLONG dlightFlashMs = 0;
    int dlightFlashStreak = 0;
    ULONGLONG swayLastRealMs = 0;
    dlight_t dummyFlashDlight{};

    std::unordered_set<int> hudSprHandles;
    bool curSprIsHud = false;

    FnLoadLibraryA   origLoadLibraryA = nullptr;
    FnStartSound     origDynSound = nullptr;
    FnStartSound     origStatSound = nullptr;
    FnInitialize     origInitialize = nullptr;
    FnHudRedraw      origHudRedraw = nullptr;
    FnHudVidInit     origHudVidInit = nullptr;
    FnGetScreenInfo  origGetScreenInfo = nullptr;
    FnCalcRefdef     origCalcRefdef = nullptr;
    FnHudReset       origHudReset = nullptr;
    FnTextMsgGet     origTextMsgGet = nullptr;
    FnWglSwapBuffers origWglSwapBuffers = nullptr;
    FnLoadgame       origLoadgame = nullptr;
    FnHookUserMsg    origHookUserMsg = nullptr;
    FnUserMsg        origMsgBattery = nullptr;
    FnUserMsg        origMsgHealth = nullptr;
    FnUserMsg        origMsgDamage = nullptr;
    FnUserMsg        origMsgWeapPickup = nullptr;
    FnUserMsg        origMsgFlashlight = nullptr;
    FnAllocDlight    origAllocDlight = nullptr;
    FnSprLoad        origSprLoad = nullptr;
    FnSprSet         origSprSet = nullptr;
    FnSprDraw        origSprDraw = nullptr;
    FnSprDraw        origSprDrawHoles = nullptr;
    FnSprDraw        origSprDrawAdditive = nullptr;
    FnFillRgba       origFillRgba = nullptr;
    FnDrawChar       origDrawChar = nullptr;
    FnTriBegin       origTriBegin = nullptr;
    FnTriEnd         origTriEnd = nullptr;

    bool IsFlashlightActiveLocked() const noexcept {
        if (flashlightMsgOn) return true;
        ULONGLONG now = GetTickCount64();
        return (dlightFlashStreak >= 2 && dlightFlashMs != 0 && (now - dlightFlashMs) < 160);
    }

    void ClearAllBuffersLocked() {
        captions.ClearBuffersLocked();
        flashlightMsgOn = false;
        dlightFlashMs = 0;
        dlightFlashStreak = 0;
    }
};

extern EngineContext g_ctx;

void TryHookClient(HMODULE cl);
void InitMod();

} // namespace goldsrc
