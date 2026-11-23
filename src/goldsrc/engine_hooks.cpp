#include "goldsrc/engine_hooks.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

#include "core/log.hpp"
#include "goldsrc/hook_x86.hpp"
#include "goldsrc/sound_handler.hpp"

namespace goldsrc {

EngineContext g_ctx;

namespace {

CRITICAL_SECTION g_hookCs;
bool g_hookCsInit = false;

struct HookGuard {
    HookGuard() { if (g_hookCsInit) EnterCriticalSection(&g_hookCs); }
    ~HookGuard() { if (g_hookCsInit) LeaveCriticalSection(&g_hookCs); }
};

void __cdecl Cmd_SubtitlesReload() {
    if (g_ctx.engfuncs && g_ctx.engfuncs->pfnGetGameDirectory) {
        const char* gd = g_ctx.engfuncs->pfnGetGameDirectory();
        if (gd && *gd) g_ctx.captions.activeGameDir = gd;
    }
    g_ctx.captions.ReloadAllFromDisk(true);
    if (g_ctx.engfuncs && g_ctx.engfuncs->Con_Printf) {
        g_ctx.engfuncs->Con_Printf("[Subtitles] Reloaded (%s): %u captions, %u titles.\n",
                                   g_ctx.captions.activeGameDir.c_str(),
                                   static_cast<unsigned>(g_ctx.captions.db.subtitles.size()),
                                   static_cast<unsigned>(g_ctx.captions.db.titles.size()));
    }
}

void __cdecl Cmd_SubtitlesClear() {
    AcquireSRWLockExclusive(&g_ctx.captions.lock);
    g_ctx.ClearAllBuffersLocked();
    ReleaseSRWLockExclusive(&g_ctx.captions.lock);
}

void __cdecl Cmd_HevVoice() {
    g_ctx.hev.ToggleVoice(g_ctx.engfuncs);
}

BOOL WINAPI Hook_WglSwapBuffers(HDC hdc) {
    if (g_ctx.hudFboCapturedThisFrame && g_ctx.screenW > 0 && g_ctx.screenH > 0 && g_ctx.gl.Load().has_value()) {
        g_ctx.hudFboCapturedThisFrame = false;
        g_ctx.hudFbo.CompositeScaled(g_ctx.gl, g_ctx.screenW, g_ctx.screenH, g_ctx.captions.cfg.hud_scale);
    }
    g_ctx.overlay.Render(g_ctx.gl, g_ctx.captions, g_ctx.screenW, g_ctx.screenH);
    return g_ctx.origWglSwapBuffers ? g_ctx.origWglSwapBuffers(hdc) : TRUE;
}

int __cdecl Hook_HUD_Redraw(float time, int intermission) {
    auto& st = g_ctx.captions;
    if (st.cfg.enable_gl_flashlight && !intermission && g_ctx.screenW > 0 && g_ctx.screenH > 0 && g_ctx.gl.Load().has_value()) {
        if (HGLRC ctx = g_ctx.gl.wglGetCurrentContext()) {
            AcquireSRWLockShared(&st.lock);
            bool active = !st.IsGamePausedLocked(GetTickCount64()) && g_ctx.IsFlashlightActiveLocked();
            float intensity = st.cfg.gl_flashlight_intensity;
            ReleaseSRWLockShared(&st.lock);
            if (active) {
                g_ctx.glFlashlight.Render(g_ctx.gl, ctx, g_ctx.screenW, g_ctx.screenH, intensity);
            }
        }
    }
    g_ctx.prevHealthMaxX = g_ctx.curHealthMaxX;
    g_ctx.prevArmorMinX = g_ctx.curArmorMinX;
    g_ctx.curHealthMaxX = 0;
    g_ctx.curArmorMinX = 100000;

    bool useFbo = !intermission && st.cfg.hud_scale > 1.01f &&
                  g_ctx.screenW >= 800 && g_ctx.screenH >= 600 && g_ctx.gl.Load().has_value();
    bool captured = false;
    if (useFbo) {
        if (HGLRC ctx = g_ctx.gl.wglGetCurrentContext()) {
            captured = g_ctx.hudFbo.BeginCapture(g_ctx.gl, ctx, g_ctx.screenW, g_ctx.screenH);
        }
    }
    int ret = g_ctx.origHudRedraw ? g_ctx.origHudRedraw(time, intermission) : 0;
    if (captured) {
        g_ctx.hudFbo.EndCapture();
        g_ctx.hudFboCapturedThisFrame = true;
    }
    st.CheckIniHotReload();
    if (!g_ctx.engfuncs || !g_ctx.engfuncs->pfnGetScreenInfo) return ret;

    SCREENINFO si{};
    si.iSize = sizeof(si);
    g_ctx.engfuncs->pfnGetScreenInfo(&si);
    if (si.iWidth <= 0 || si.iHeight <= 0) return ret;

    float curClientTime = (g_ctx.engfuncs->GetClientTime) ? g_ctx.engfuncs->GetClientTime() : time;
    ULONGLONG realNow = GetTickCount64();

    AcquireSRWLockExclusive(&st.lock);
    g_ctx.screenW = si.iWidth;
    g_ctx.screenH = si.iHeight;
    st.lastHudRedrawRealMs = realNow;
    bool rewound = (curClientTime > 0.0f && curClientTime + 0.01f < st.lastClientTime);
    if (g_ctx.resetRequested || rewound) {
        g_ctx.ClearAllBuffersLocked();
        g_ctx.resetRequested = false;
        st.clockPrevTime = curClientTime;
        st.lastTimeAdvanceRealMs = realNow;
    }
    st.lastClientTime = curClientTime;

    if (st.clockPrevTime < 0.0f) {
        st.lastTimeAdvanceRealMs = realNow;
    } else {
        float dt = curClientTime - st.clockPrevTime;
        if (dt > 0.0f) {
            st.gameMs += static_cast<ULONGLONG>(std::min(dt, 0.25f) * 1000.0f);
            st.lastTimeAdvanceRealMs = realNow;
        }
    }
    st.clockPrevTime = curClientTime;
    const ULONGLONG nowMs = st.gameMs;

    if (g_ctx.hasViewOrg) {
        for (auto& c : st.captions) {
            if (c.hasOrigin && c.fadeDist > 0.0f && c.expire_ms > nowMs + 250ULL) {
                float dx = c.origin[0] - g_ctx.viewOrg[0];
                float dy = c.origin[1] - g_ctx.viewOrg[1];
                float dz = c.origin[2] - g_ctx.viewOrg[2];
                if (dx * dx + dy * dy + dz * dz > c.fadeDist * c.fadeDist) {
                    c.expire_ms = nowMs + 250ULL;
                }
            }
        }
    }
    std::erase_if(st.captions, [nowMs](const core::ActiveCaption& c) { return c.expire_ms <= nowMs; });
    bool firePowerRestored = g_ctx.hev.ConsumePendingPowerRestoredLocked(nowMs);
    ReleaseSRWLockExclusive(&st.lock);
    if (firePowerRestored) {
        g_ctx.hev.TryTrigger(st, g_ctx.engfuncs, kHevPowerRestored, "!HEV_D00");
    }
    return ret;
}

void __cdecl Hook_V_CalcRefdef(ref_params_t* pparams) {
    if (g_ctx.origCalcRefdef) g_ctx.origCalcRefdef(pparams);
    if (pparams) {
        g_ctx.viewOrg[0] = pparams->vieworg[0];
        g_ctx.viewOrg[1] = pparams->vieworg[1];
        g_ctx.viewOrg[2] = pparams->vieworg[2];
        g_ctx.viewAngles[0] = pparams->viewangles[0];
        g_ctx.viewAngles[1] = pparams->viewangles[1];
        g_ctx.viewAngles[2] = pparams->viewangles[2];
        g_ctx.hasViewOrg = true;
        ULONGLONG now = GetTickCount64();
        float dt = (g_ctx.swayLastRealMs > 0 && now > g_ctx.swayLastRealMs)
            ? std::clamp(static_cast<float>(now - g_ctx.swayLastRealMs) * 0.001f, 0.001f, 0.1f)
            : 0.016f;
        g_ctx.swayLastRealMs = now;
        g_ctx.glFlashlight.UpdateSway(pparams->viewangles[0], pparams->viewangles[1], dt);
    }
}

void __cdecl Hook_HUD_Reset() {
    auto& st = g_ctx.captions;
    AcquireSRWLockExclusive(&st.lock);
    g_ctx.ClearAllBuffersLocked();
    g_ctx.hev.ResetLocked();
    g_ctx.glFlashlight.ResetSway();
    st.clockPrevTime = -1.0f;
    st.lastClientTime = 0.0f;
    g_ctx.hasViewOrg = false;
    ReleaseSRWLockExclusive(&st.lock);
    if (g_ctx.origHudReset) g_ctx.origHudReset();
}

int __cdecl Hook_MsgFunc_Battery(const char* pszName, int iSize, void* pbuf) {
    if (pbuf && iSize > 0) {
        std::span<const std::byte> buf{static_cast<const std::byte*>(pbuf), static_cast<size_t>(iSize)};
        int val = (buf.size() >= 2)
            ? static_cast<int>(static_cast<int16_t>(std::to_integer<uint16_t>(buf[0]) | (std::to_integer<uint16_t>(buf[1]) << 8)))
            : std::to_integer<int>(buf[0]);
        g_ctx.hev.OnBatteryValue(g_ctx.captions, g_ctx.engfuncs, val);
    }
    return g_ctx.origMsgBattery ? g_ctx.origMsgBattery(pszName, iSize, pbuf) : 1;
}

int __cdecl Hook_MsgFunc_Health(const char* pszName, int iSize, void* pbuf) {
    if (pbuf && iSize > 0) {
        std::span<const std::byte> buf{static_cast<const std::byte*>(pbuf), static_cast<size_t>(iSize)};
        int hp = std::to_integer<int>(buf[0]);
        g_ctx.hev.OnHealthValue(g_ctx.captions, g_ctx.engfuncs, hp);
    }
    return g_ctx.origMsgHealth ? g_ctx.origMsgHealth(pszName, iSize, pbuf) : 1;
}

int __cdecl Hook_MsgFunc_Damage(const char* pszName, int iSize, void* pbuf) {
    if (pbuf && iSize >= 6) {
        std::span<const std::byte> buf{static_cast<const std::byte*>(pbuf), static_cast<size_t>(iSize)};
        int dmgSave = std::to_integer<int>(buf[0]);
        int dmgTake = std::to_integer<int>(buf[1]);
        uint32_t bits = std::to_integer<uint32_t>(buf[2]) |
                        (std::to_integer<uint32_t>(buf[3]) << 8) |
                        (std::to_integer<uint32_t>(buf[4]) << 16) |
                        (std::to_integer<uint32_t>(buf[5]) << 24);
        g_ctx.hev.OnDamageEvent(g_ctx.captions, g_ctx.engfuncs, dmgSave, dmgTake, bits);
    }
    return g_ctx.origMsgDamage ? g_ctx.origMsgDamage(pszName, iSize, pbuf) : 1;
}

int __cdecl Hook_MsgFunc_WeapPickup(const char* pszName, int iSize, void* pbuf) {
    if (pbuf && iSize > 0) {
        std::span<const std::byte> buf{static_cast<const std::byte*>(pbuf), static_cast<size_t>(iSize)};
        int id = std::to_integer<int>(buf[0]);
        g_ctx.hev.OnWeapPickup(g_ctx.captions, g_ctx.engfuncs, id);
    }
    return g_ctx.origMsgWeapPickup ? g_ctx.origMsgWeapPickup(pszName, iSize, pbuf) : 1;
}

dlight_t* __cdecl Hook_CL_AllocDlight(int key) {
    int localIdx = 1;
    if (g_ctx.engfuncs && g_ctx.engfuncs->GetLocalPlayer) {
        if (void* lp = g_ctx.engfuncs->GetLocalPlayer()) {
            int idx = *reinterpret_cast<const int*>(lp);
            if (idx > 0 && idx <= 64) localIdx = idx;
        }
    }
    if (key == localIdx) {
        ULONGLONG now = GetTickCount64();
        bool suppressVanilla = false;
        AcquireSRWLockExclusive(&g_ctx.captions.lock);
        if (g_ctx.dlightFlashMs != 0 && (now - g_ctx.dlightFlashMs) < 120) {
            if (g_ctx.dlightFlashStreak < 10) ++g_ctx.dlightFlashStreak;
        } else {
            g_ctx.dlightFlashStreak = 1;
        }
        g_ctx.dlightFlashMs = now;
        suppressVanilla = g_ctx.captions.cfg.enable_gl_flashlight &&
                          g_ctx.captions.cfg.gl_flashlight_hide_vanilla &&
                          !g_ctx.glFlashlight.HasFailed() &&
                          g_ctx.IsFlashlightActiveLocked();
        ReleaseSRWLockExclusive(&g_ctx.captions.lock);
        if (suppressVanilla) {
            std::memset(&g_ctx.dummyFlashDlight, 0, sizeof(g_ctx.dummyFlashDlight));
            return &g_ctx.dummyFlashDlight;
        }
    } else if (g_ctx.flashlightMsgOn) {
        static int s_loggedOtherKey = -999999;
        if (s_loggedOtherKey != key) {
            s_loggedOtherKey = key;
            core::LogMsg("[gl-flash] dlight key=%d (localIdx=%d)", key, localIdx);
        }
    }
    return g_ctx.origAllocDlight ? g_ctx.origAllocDlight(key) : &g_ctx.dummyFlashDlight;
}

int __cdecl Hook_MsgFunc_Flashlight(const char* pszName, int iSize, void* pbuf) {
    if (pbuf && iSize >= 1) {
        std::span<const std::byte> buf{static_cast<const std::byte*>(pbuf), static_cast<size_t>(iSize)};
        bool on = (std::to_integer<int>(buf[0]) != 0);
        AcquireSRWLockExclusive(&g_ctx.captions.lock);
        g_ctx.flashlightMsgOn = on;
        if (!on) {
            g_ctx.dlightFlashStreak = 0;
            g_ctx.dlightFlashMs = 0;
        }
        ReleaseSRWLockExclusive(&g_ctx.captions.lock);
    }
    return g_ctx.origMsgFlashlight ? g_ctx.origMsgFlashlight(pszName, iSize, pbuf) : 1;
}

int __cdecl Hook_SPR_Load(const char* szPicName) {
    int h = g_ctx.origSprLoad ? g_ctx.origSprLoad(szPicName) : 0;
    if (h > 0 && szPicName) {
        std::string s = subs::Config::Lower(szPicName);
        if (s.contains("640hud") || s.contains("320hud") || s.contains("ctf_hud")) {
            g_ctx.hudSprHandles.insert(h);
        } else {
            g_ctx.hudSprHandles.erase(h);
        }
    }
    return h;
}

void __cdecl Hook_SPR_Set(int hPic, int r, int g, int b) {
    g_ctx.curSprIsHud = g_ctx.hudSprHandles.contains(hPic);
    if (g_ctx.origSprSet) g_ctx.origSprSet(hPic, r, g, b);
}

int CompactHudAdjustX(int x, int y, int approxW, bool isHudElem) {
    if (!isHudElem || !g_ctx.captions.cfg.hud_compact_armor) return x;
    if (g_ctx.screenW < 800 || g_ctx.screenH <= 0) return x;
    if (y < g_ctx.screenH - 80 || x < 0 || x >= g_ctx.screenW / 2) return x;

    const int splitX = std::max(130, g_ctx.screenW / 8);
    if (x < splitX) {
        g_ctx.curHealthMaxX = std::max(g_ctx.curHealthMaxX, x + approxW);
        return x;
    }
    g_ctx.curArmorMinX = std::min(g_ctx.curArmorMinX, x);
    if (g_ctx.prevHealthMaxX > 0 && g_ctx.prevArmorMinX < g_ctx.screenW) {
        int shift = std::max(0, g_ctx.prevArmorMinX - (g_ctx.prevHealthMaxX + 14));
        return std::max(0, x - shift);
    }
    return x;
}

void __cdecl Hook_SPR_Draw(int frame, int x, int y, const void* prc) {
    int sprW = 24;
    if (prc) {
        const int* rc = static_cast<const int*>(prc);
        if (rc[2] > rc[0]) sprW = rc[2] - rc[0];
    }
    x = CompactHudAdjustX(x, y, sprW, g_ctx.curSprIsHud);
    if (g_ctx.origSprDraw) g_ctx.origSprDraw(frame, x, y, prc);
}

void __cdecl Hook_SPR_DrawHoles(int frame, int x, int y, const void* prc) {
    int sprW = 24;
    if (prc) {
        const int* rc = static_cast<const int*>(prc);
        if (rc[2] > rc[0]) sprW = rc[2] - rc[0];
    }
    x = CompactHudAdjustX(x, y, sprW, g_ctx.curSprIsHud);
    if (g_ctx.origSprDrawHoles) g_ctx.origSprDrawHoles(frame, x, y, prc);
}

void __cdecl Hook_SPR_DrawAdditive(int frame, int x, int y, const void* prc) {
    int sprW = 24;
    if (prc) {
        const int* rc = static_cast<const int*>(prc);
        if (rc[2] > rc[0]) sprW = rc[2] - rc[0];
    }
    x = CompactHudAdjustX(x, y, sprW, g_ctx.curSprIsHud);
    if (g_ctx.origSprDrawAdditive) g_ctx.origSprDrawAdditive(frame, x, y, prc);
}

void __cdecl Hook_FillRGBA(int x, int y, int width, int height, int r, int g, int b, int a) {
    if (width <= 8) x = CompactHudAdjustX(x, y, width, true);
    if (g_ctx.hudFbo.IsCapturing() && (width >= g_ctx.screenW / 2 || (width >= 64 && height >= 64))) {
        g_ctx.hudFbo.PauseCapture();
        if (g_ctx.origFillRgba) g_ctx.origFillRgba(x, y, width, height, r, g, b, a);
        g_ctx.hudFbo.ResumeCapture();
        return;
    }
    if (g_ctx.origFillRgba) g_ctx.origFillRgba(x, y, width, height, r, g, b, a);
}

void __cdecl Hook_TriBegin(int primitiveCode) {
    if (g_ctx.hudFbo.IsCapturing()) g_ctx.hudFbo.PauseCapture();
    if (g_ctx.origTriBegin) g_ctx.origTriBegin(primitiveCode);
}

void __cdecl Hook_TriEnd() {
    if (g_ctx.origTriEnd) g_ctx.origTriEnd();
    if (g_ctx.hudFbo.IsCapturing()) g_ctx.hudFbo.ResumeCapture();
}

int __cdecl Hook_DrawCharacter(int x, int y, int number, int r, int g, int b) {
    bool isRightHudNumber = (g_ctx.screenW > 0 && x >= (g_ctx.screenW * 88) / 100 && !(r == g && g == b));
    if (g_ctx.hudFbo.IsCapturing() && !isRightHudNumber) {
        g_ctx.hudFbo.PauseCapture();
        int res = g_ctx.origDrawChar ? g_ctx.origDrawChar(x, y, number, r, g, b) : 0;
        g_ctx.hudFbo.ResumeCapture();
        return res;
    }
    return g_ctx.origDrawChar ? g_ctx.origDrawChar(x, y, number, r, g, b) : 0;
}

int __cdecl Hook_pfnHookUserMsg(const char* szMsgName, void* pfn) {
    if (szMsgName && pfn) {
        auto hookMsg = [&](const char* name, FnUserMsg& orig, void* detour) -> bool {
            if (std::strcmp(szMsgName, name) == 0 && pfn != detour) {
                orig = reinterpret_cast<FnUserMsg>(pfn);
                pfn = detour;
                return true;
            }
            return false;
        };
        if (!hookMsg("Battery",    g_ctx.origMsgBattery,    reinterpret_cast<void*>(Hook_MsgFunc_Battery)) &&
            !hookMsg("Health",     g_ctx.origMsgHealth,     reinterpret_cast<void*>(Hook_MsgFunc_Health)) &&
            !hookMsg("Damage",     g_ctx.origMsgDamage,     reinterpret_cast<void*>(Hook_MsgFunc_Damage)) &&
            !hookMsg("WeapPickup", g_ctx.origMsgWeapPickup, reinterpret_cast<void*>(Hook_MsgFunc_WeapPickup))) {
            hookMsg("Flashlight",  g_ctx.origMsgFlashlight, reinterpret_cast<void*>(Hook_MsgFunc_Flashlight));
        }
    }
    return g_ctx.origHookUserMsg ? g_ctx.origHookUserMsg(szMsgName, pfn) : 0;
}

template <typename Fn>
void PatchEngSlot(Fn& slot, Fn& orig, Fn detour) {
    if (slot && slot != detour) {
        if (!orig) orig = slot;
        DWORD oldProt = 0;
        if (VirtualProtect(&slot, sizeof(void*), PAGE_READWRITE, &oldProt)) {
            slot = detour;
            VirtualProtect(&slot, sizeof(void*), oldProt, &oldProt);
        }
    }
}

void HookClientEngfuncs(cl_enginefunc_t* eng) {
    if (!eng) return;
    if (!g_ctx.engfuncs) g_ctx.engfuncs = eng;
    PatchEngSlot(eng->pfnTextMessageGet, g_ctx.origTextMsgGet, Hook_TextMessageGet);
    PatchEngSlot(eng->pfnHookUserMsg,    g_ctx.origHookUserMsg,
                 reinterpret_cast<int(__cdecl*)(const char*, void*)>(Hook_pfnHookUserMsg));
    if (eng->pEfxAPI && eng->pEfxAPI->CL_AllocDlight && !g_ctx.origAllocDlight) {
        void* pAlloc = reinterpret_cast<void*>(eng->pEfxAPI->CL_AllocDlight);
        if (void* orig = hook::InstallInlineHook(pAlloc, reinterpret_cast<void*>(Hook_CL_AllocDlight))) {
            g_ctx.origAllocDlight = reinterpret_cast<FnAllocDlight>(orig);
            core::LogMsg("[gl-flash] Hooked CL_AllocDlight at %p -> %p", pAlloc, orig);
        }
    }
    if (g_ctx.captions.cfg.hud_compact_armor || g_ctx.captions.cfg.hud_scale > 1.01f) {
        PatchEngSlot(eng->pfnSPR_Load,         g_ctx.origSprLoad,         Hook_SPR_Load);
        PatchEngSlot(eng->pfnSPR_Set,          g_ctx.origSprSet,          Hook_SPR_Set);
        PatchEngSlot(eng->pfnSPR_Draw,         g_ctx.origSprDraw,         Hook_SPR_Draw);
        PatchEngSlot(eng->pfnSPR_DrawHoles,    g_ctx.origSprDrawHoles,    Hook_SPR_DrawHoles);
        PatchEngSlot(eng->pfnSPR_DrawAdditive, g_ctx.origSprDrawAdditive, Hook_SPR_DrawAdditive);
        PatchEngSlot(eng->pfnFillRGBA,         g_ctx.origFillRgba,        Hook_FillRGBA);
        PatchEngSlot(eng->pfnDrawCharacter,    g_ctx.origDrawChar,        Hook_DrawCharacter);
        if (eng->pTriAPI) {
            PatchEngSlot(eng->pTriAPI->Begin, g_ctx.origTriBegin, Hook_TriBegin);
            PatchEngSlot(eng->pTriAPI->End,   g_ctx.origTriEnd,   Hook_TriEnd);
        }
    }
}

cl_enginefunc_t* FindClientEngfuncs(HMODULE cl, void* pInit) {
    if (!pInit) return nullptr;
    std::span<const uint8_t> code{static_cast<const uint8_t*>(pInit), 160};
    for (size_t i = 0; i + 6 < std::min<size_t>(code.size(), 64); ++i) {
        if (code[i] == 0xBF && code[i + 5] == 0xF3 && code[i + 6] == 0xA5) {
            uintptr_t addr = hook::ReadUnaligned<uintptr_t>(code.subspan(i + 1, sizeof(uintptr_t)));
            if (addr > 0x10000) return reinterpret_cast<cl_enginefunc_t*>(addr);
        }
    }
    if (!cl) return nullptr;
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(cl);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const uint8_t*>(cl) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    uintptr_t base = reinterpret_cast<uintptr_t>(cl);
    uintptr_t end  = base + nt->OptionalHeader.SizeOfImage;
    for (size_t i = 0; i + 5 <= code.size(); ++i) {
        uint8_t op = code[i];
        if (op == 0x68 || (op >= 0xB8 && op <= 0xBF)) {
            uintptr_t addr = hook::ReadUnaligned<uintptr_t>(code.subspan(i + 1, sizeof(uintptr_t)));
            if (addr >= base + 0x1000 && addr + sizeof(cl_enginefunc_t) <= end) {
                auto* cand = reinterpret_cast<cl_enginefunc_t*>(addr);
                if (cand->pfnGetScreenInfo && cand->pfnHookUserMsg && cand->pfnGetGameDirectory) {
                    return cand;
                }
            }
        }
    }
    return nullptr;
}

int __cdecl Hook_Initialize(cl_enginefunc_t* pEnginefuncs, int iVersion) {
    if (pEnginefuncs && pEnginefuncs->pfnGetGameDirectory) {
        const char* gd = pEnginefuncs->pfnGetGameDirectory();
        if (gd && *gd && subs::Config::Lower(gd) != subs::Config::Lower(g_ctx.captions.activeGameDir)) {
            g_ctx.captions.activeGameDir = gd;
            g_ctx.captions.ReloadAllFromDisk(true);
        }
    }
    HookClientEngfuncs(pEnginefuncs);
    int r = g_ctx.origInitialize ? g_ctx.origInitialize(pEnginefuncs, iVersion) : 0;
    g_ctx.engfuncs = pEnginefuncs;
    if (pEnginefuncs) {
        if (pEnginefuncs->pfnTextMessageGet && !g_ctx.origTextMsgGet &&
            pEnginefuncs->pfnTextMessageGet != Hook_TextMessageGet) {
            g_ctx.origTextMsgGet = reinterpret_cast<FnTextMsgGet>(
                hook::InstallInlineHook(reinterpret_cast<void*>(pEnginefuncs->pfnTextMessageGet),
                                        reinterpret_cast<void*>(Hook_TextMessageGet)));
            core::LogMsg("[client] Hooked pfnTextMessageGet at %p -> %p",
                         reinterpret_cast<void*>(pEnginefuncs->pfnTextMessageGet),
                         reinterpret_cast<void*>(g_ctx.origTextMsgGet));
        }
        if (HMODULE cl = GetModuleHandleA("client.dll")) {
            if (cl_enginefunc_t* clientEng = FindClientEngfuncs(cl, reinterpret_cast<void*>(GetProcAddress(cl, "Initialize")))) {
                HookClientEngfuncs(clientEng);
            }
        }
        if (pEnginefuncs->pfnAddCommand) {
            pEnginefuncs->pfnAddCommand("subtitles_reload", Cmd_SubtitlesReload);
            pEnginefuncs->pfnAddCommand("subtitles_clear", Cmd_SubtitlesClear);
            pEnginefuncs->pfnAddCommand("hev_voice", Cmd_HevVoice);
        }
    }
    return r;
}

void __cdecl Hook_Host_Loadgame_f() {
    auto& st = g_ctx.captions;
    AcquireSRWLockExclusive(&st.lock);
    g_ctx.ClearAllBuffersLocked();
    g_ctx.hev.ResetLocked();
    st.clockPrevTime = -1.0f;
    st.lastClientTime = 0.0f;
    g_ctx.resetRequested = false;
    g_ctx.hasViewOrg = false;
    ReleaseSRWLockExclusive(&st.lock);
    core::LogMsg("[load] Host_Loadgame_f cleared caption buffers");
    if (g_ctx.origLoadgame) g_ctx.origLoadgame();
}

void TryHookHw(HMODULE hw) {
    HookGuard guard;
    if (g_ctx.hwHooked || !hw) return;
    void* dyn  = hook::FindFunctionByStringXref(hw, "S_StartDynamicSound: %s volume > 255");
    void* stat = hook::FindFunctionByStringXref(hw, "S_StartStaticSound: %s volume > 255");
    void* load = hook::FindFunctionByStringXref(hw, "Loading game from");
    if (dyn)  g_ctx.origDynSound  = reinterpret_cast<FnStartSound>(hook::InstallInlineHook(dyn, reinterpret_cast<void*>(Hook_S_StartDynamicSound)));
    if (stat) g_ctx.origStatSound = reinterpret_cast<FnStartSound>(hook::InstallInlineHook(stat, reinterpret_cast<void*>(Hook_S_StartStaticSound)));
    if (load) g_ctx.origLoadgame  = reinterpret_cast<FnLoadgame>(hook::InstallInlineHook(load, reinterpret_cast<void*>(Hook_Host_Loadgame_f)));
    g_ctx.hwHooked = (g_ctx.origDynSound != nullptr || g_ctx.origStatSound != nullptr);
    core::LogMsg("[hw] dyn=%p->%p stat=%p->%p load=%p->%p",
                 dyn, reinterpret_cast<void*>(g_ctx.origDynSound),
                 stat, reinterpret_cast<void*>(g_ctx.origStatSound),
                 load, reinterpret_cast<void*>(g_ctx.origLoadgame));
}

void TryHookOpenGL(HMODULE gl) {
    HookGuard guard;
    if (g_ctx.glHooked || !gl) return;
    void* pSwap = reinterpret_cast<void*>(GetProcAddress(gl, "wglSwapBuffers"));
    if (pSwap) {
        g_ctx.origWglSwapBuffers = reinterpret_cast<FnWglSwapBuffers>(
            hook::InstallInlineHook(pSwap, reinterpret_cast<void*>(Hook_WglSwapBuffers)));
        g_ctx.glHooked = (g_ctx.origWglSwapBuffers != nullptr);
    }
    core::LogMsg("[gl] wglSwapBuffers=%p->%p", pSwap, reinterpret_cast<void*>(g_ctx.origWglSwapBuffers));
}

HMODULE WINAPI Hook_LoadLibraryA(LPCSTR lpLibFileName) {
    HMODULE m = g_ctx.origLoadLibraryA ? g_ctx.origLoadLibraryA(lpLibFileName) : nullptr;
    if (m && lpLibFileName) {
        std::string s = subs::Config::Lower(lpLibFileName);
        if (s.contains("hw.dll")) TryHookHw(m);
        else if (s.contains("client.dll")) TryHookClient(m);
        else if (s.contains("opengl32.dll")) TryHookOpenGL(m);
    }
    return m;
}

} // namespace

void TryHookClient(HMODULE cl) {
    HookGuard guard;
    if (!cl) return;
    void* pInit   = reinterpret_cast<void*>(GetProcAddress(cl, "Initialize"));
    void* pRedraw = reinterpret_cast<void*>(GetProcAddress(cl, "HUD_Redraw"));
    if (g_ctx.clientHooked && pRedraw && *static_cast<const uint8_t*>(pRedraw) == 0xE9) return;

    g_ctx.origInitialize = nullptr;
    g_ctx.origHudRedraw  = nullptr;
    g_ctx.origHudVidInit = nullptr;
    g_ctx.origCalcRefdef = nullptr;
    g_ctx.origHudReset   = nullptr;
    g_ctx.origMsgBattery = nullptr;
    g_ctx.origMsgHealth = nullptr;
    g_ctx.origMsgDamage = nullptr;
    g_ctx.origMsgWeapPickup = nullptr;
    g_ctx.origMsgFlashlight = nullptr;
    g_ctx.hudSprHandles.clear();
    g_ctx.curSprIsHud = false;

    void* pCalc  = reinterpret_cast<void*>(GetProcAddress(cl, "V_CalcRefdef"));
    void* pReset = reinterpret_cast<void*>(GetProcAddress(cl, "HUD_Reset"));
    cl_enginefunc_t* clientEng = FindClientEngfuncs(cl, pInit);
    if (clientEng && clientEng->pfnGetScreenInfo) {
        HookClientEngfuncs(clientEng);
    }
    if (pInit)   g_ctx.origInitialize = reinterpret_cast<FnInitialize>(hook::InstallInlineHook(pInit, reinterpret_cast<void*>(Hook_Initialize)));
    if (pRedraw) g_ctx.origHudRedraw  = reinterpret_cast<FnHudRedraw>(hook::InstallInlineHook(pRedraw, reinterpret_cast<void*>(Hook_HUD_Redraw)));
    if (pCalc)   g_ctx.origCalcRefdef = reinterpret_cast<FnCalcRefdef>(hook::InstallInlineHook(pCalc, reinterpret_cast<void*>(Hook_V_CalcRefdef)));
    if (pReset)  g_ctx.origHudReset   = reinterpret_cast<FnHudReset>(hook::InstallInlineHook(pReset, reinterpret_cast<void*>(Hook_HUD_Reset)));
    g_ctx.clientHooked = (g_ctx.origInitialize != nullptr && g_ctx.origHudRedraw != nullptr);
    core::LogMsg("[client] mod=%p init=%p->%p redraw=%p->%p calc=%p->%p reset=%p->%p clientEng=%p",
                 reinterpret_cast<void*>(cl),
                 pInit, reinterpret_cast<void*>(g_ctx.origInitialize),
                 pRedraw, reinterpret_cast<void*>(g_ctx.origHudRedraw),
                 pCalc, reinterpret_cast<void*>(g_ctx.origCalcRefdef),
                 pReset, reinterpret_cast<void*>(g_ctx.origHudReset),
                 reinterpret_cast<void*>(clientEng));
}

void InitMod() {
    InitializeCriticalSection(&g_hookCs);
    g_hookCsInit = true;
    char exePath[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string ep = exePath;
    size_t sl = ep.find_last_of("\\/");
    auto& st = g_ctx.captions;
    st.exeDir = (sl != std::string::npos) ? ep.substr(0, sl) : ".";
    st.iniPath = st.exeDir + "\\subtitles.ini";
    st.activeGameDir = core::ParseCmdlineGameDir();

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExA(st.iniPath.c_str(), GetFileExInfoStandard, &fad)) {
        st.iniMtime = fad.ftLastWriteTime;
    }
    st.ReloadAllFromDisk(true);

    if (HMODULE hw = GetModuleHandleA("hw.dll")) TryHookHw(hw);
    if (HMODULE cl = GetModuleHandleA("client.dll")) TryHookClient(cl);
    if (HMODULE gl = GetModuleHandleA("opengl32.dll")) TryHookOpenGL(gl);
    if (HMODULE k32 = GetModuleHandleA("kernel32.dll")) {
        void* pLL = reinterpret_cast<void*>(GetProcAddress(k32, "LoadLibraryA"));
        if (pLL) {
            g_ctx.origLoadLibraryA = reinterpret_cast<FnLoadLibraryA>(
                hook::InstallInlineHook(pLL, reinterpret_cast<void*>(Hook_LoadLibraryA)));
        }
    }
}

} // namespace goldsrc
