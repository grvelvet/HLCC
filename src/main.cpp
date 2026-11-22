/**
 * @file main.cpp
 * @brief DLL entry point and x86 inline/engine hooks for hw.dll, client.dll, and opengl32.dll.
 *
 * Hooks installed:
 *  - hw.dll:       S_StartDynamicSound, S_StartStaticSound, Host_Loadgame_f
 *  - opengl32.dll: wglSwapBuffers (renders subtitle/title/HUD overlays)
 *  - client.dll:   Initialize, HUD_Redraw, V_CalcRefdef, HUD_Reset, UserMsgs, SPR_*
 *                  (SPR/FillRGBA/TriAPI hooks are active when HUD scaling or compact armor is enabled).
 */

#include "app_state.hpp"
#include "text_layout.hpp"
#include "overlay_render.hpp"
#include "sound_handler.hpp"

int __cdecl Hook_MsgFunc_Battery(const char* pszName, int iSize, void* pbuf) {
    if (pbuf && iSize > 0) {
        std::span<const std::byte> buf{static_cast<const std::byte*>(pbuf), static_cast<size_t>(iSize)};
        int val = (buf.size() >= 2)
            ? static_cast<int>(static_cast<int16_t>(std::to_integer<uint16_t>(buf[0]) | (std::to_integer<uint16_t>(buf[1]) << 8)))
            : std::to_integer<int>(buf[0]);
        OnBatteryValue(val);
    }
    return g_origMsgBattery ? g_origMsgBattery(pszName, iSize, pbuf) : 1;
}

int __cdecl Hook_MsgFunc_Health(const char* pszName, int iSize, void* pbuf) {
    if (pbuf && iSize > 0) {
        std::span<const std::byte> buf{static_cast<const std::byte*>(pbuf), static_cast<size_t>(iSize)};
        int hp = std::to_integer<int>(buf[0]);
        OnHealthValue(hp);
    }
    return g_origMsgHealth ? g_origMsgHealth(pszName, iSize, pbuf) : 1;
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
        OnDamageEvent(dmgSave, dmgTake, bits);
    }
    return g_origMsgDamage ? g_origMsgDamage(pszName, iSize, pbuf) : 1;
}

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

void OnWeapPickup(int weapId) {
    if (weapId < 0 || weapId >= 32) return;
    const char* sentence = WeaponIdToHevSentence(weapId);
    if (!sentence) return;

    AcquireSRWLockExclusive(&g_lock);
    const ULONGLONG nowMs = g_gameMs;
    uint32_t bit = (1u << weapId);
    bool ok = g_cfg.restore_unused_hev && g_cfg.enable_hev && g_hevVoiceEnabled &&
              nowMs >= 1000 && (g_announcedWeaponsMask & bit) == 0 &&
              (g_lastNativeHevEndMs == 0 || nowMs >= g_lastNativeHevEndMs + 1000ULL);
    if (ok) {
        g_announcedWeaponsMask |= bit;
        g_lastNativeHevMs = nowMs;
        g_lastNativeHevEndMs = std::max(g_lastNativeHevEndMs, nowMs + 2500ULL);
    }
    ReleaseSRWLockExclusive(&g_lock);

    if (ok) {
        PlayHevSentence(sentence);
        LogMsg("[hev-restore] weapon=%d -> %s", weapId, sentence);
    }
}

int __cdecl Hook_MsgFunc_WeapPickup(const char* pszName, int iSize, void* pbuf) {
    if (pbuf && iSize > 0) {
        std::span<const std::byte> buf{static_cast<const std::byte*>(pbuf), static_cast<size_t>(iSize)};
        int id = std::to_integer<int>(buf[0]);
        OnWeapPickup(id);
    }
    return g_origMsgWeapPickup ? g_origMsgWeapPickup(pszName, iSize, pbuf) : 1;
}

dlight_t* __cdecl Hook_CL_AllocDlight(int key) {
    int localIdx = 1;
    if (g_engfuncs && g_engfuncs->GetLocalPlayer) {
        if (void* lp = g_engfuncs->GetLocalPlayer()) {
            int idx = *reinterpret_cast<const int*>(lp);
            if (idx > 0 && idx <= 64) localIdx = idx;
        }
    }
    if (key == localIdx) {
        ULONGLONG now = GetTickCount64();
        bool suppressVanilla = false;
        AcquireSRWLockExclusive(&g_lock);
        if (g_dlightFlashMs != 0 && (now - g_dlightFlashMs) < 120) {
            if (g_dlightFlashStreak < 10) ++g_dlightFlashStreak;
        } else {
            g_dlightFlashStreak = 1;
        }
        g_dlightFlashMs = now;
        suppressVanilla = g_cfg.enable_gl_flashlight && g_cfg.gl_flashlight_hide_vanilla &&
                          !g_glFlashlight.HasFailed() && IsFlashlightActiveLocked();
        ReleaseSRWLockExclusive(&g_lock);
        if (suppressVanilla) {
            std::memset(&g_dummyFlashDlight, 0, sizeof(g_dummyFlashDlight));
            return &g_dummyFlashDlight;
        }
    }
    return g_origAllocDlight ? g_origAllocDlight(key) : &g_dummyFlashDlight;
}

int __cdecl Hook_MsgFunc_Flashlight(const char* pszName, int iSize, void* pbuf) {
    if (pbuf && iSize >= 1) {
        std::span<const std::byte> buf{static_cast<const std::byte*>(pbuf), static_cast<size_t>(iSize)};
        bool on = (std::to_integer<int>(buf[0]) != 0);
        AcquireSRWLockExclusive(&g_lock);
        g_flashlightMsgOn = on;
        if (!on) {
            g_dlightFlashStreak = 0;
            g_dlightFlashMs = 0;
        }
        ReleaseSRWLockExclusive(&g_lock);
    }
    return g_origMsgFlashlight ? g_origMsgFlashlight(pszName, iSize, pbuf) : 1;
}

int __cdecl Hook_SPR_Load(const char* szPicName) {
    int h = g_origSprLoad ? g_origSprLoad(szPicName) : 0;
    if (h > 0 && szPicName) {
        std::string s = subs::Config::Lower(szPicName);
        if (s.contains("640hud") || s.contains("320hud") || s.contains("ctf_hud")) {
            g_hudSprHandles.insert(h);
        } else {
            g_hudSprHandles.erase(h);
        }
    }
    return h;
}

void __cdecl Hook_SPR_Set(int hPic, int r, int g, int b) {
    g_curSprIsHud = g_hudSprHandles.contains(hPic);
    if (g_origSprSet) g_origSprSet(hPic, r, g, b);
}

int CompactHudAdjustX(int x, int y, int approxW, bool isHudElem) {
    if (!isHudElem || !g_cfg.hud_compact_armor) return x;
    if (g_screenW < 800 || g_screenH <= 0) return x;
    if (y < g_screenH - 80 || x < 0 || x >= g_screenW / 2) return x;

    const int splitX = std::max(130, g_screenW / 8);
    if (x < splitX) {
        g_curHealthMaxX = std::max(g_curHealthMaxX, x + approxW);
        return x;
    }
    g_curArmorMinX = std::min(g_curArmorMinX, x);
    if (g_prevHealthMaxX > 0 && g_prevArmorMinX < g_screenW) {
        int shift = std::max(0, g_prevArmorMinX - (g_prevHealthMaxX + 14));
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
    x = CompactHudAdjustX(x, y, sprW, g_curSprIsHud);
    if (g_origSprDraw) g_origSprDraw(frame, x, y, prc);
}
void __cdecl Hook_SPR_DrawHoles(int frame, int x, int y, const void* prc) {
    int sprW = 24;
    if (prc) {
        const int* rc = static_cast<const int*>(prc);
        if (rc[2] > rc[0]) sprW = rc[2] - rc[0];
    }
    x = CompactHudAdjustX(x, y, sprW, g_curSprIsHud);
    if (g_origSprDrawHoles) g_origSprDrawHoles(frame, x, y, prc);
}
void __cdecl Hook_SPR_DrawAdditive(int frame, int x, int y, const void* prc) {
    int sprW = 24;
    if (prc) {
        const int* rc = static_cast<const int*>(prc);
        if (rc[2] > rc[0]) sprW = rc[2] - rc[0];
    }
    x = CompactHudAdjustX(x, y, sprW, g_curSprIsHud);
    if (g_origSprDrawAdditive) g_origSprDrawAdditive(frame, x, y, prc);
}
void __cdecl Hook_FillRGBA(int x, int y, int width, int height, int r, int g, int b, int a) {
    if (width <= 8) x = CompactHudAdjustX(x, y, width, true);
    if (g_hudFbo.IsCapturing() && (width >= g_screenW / 2 || (width >= 64 && height >= 64))) {
        g_hudFbo.PauseCapture();
        if (g_origFillRgba) g_origFillRgba(x, y, width, height, r, g, b, a);
        g_hudFbo.ResumeCapture();
        return;
    }
    if (g_origFillRgba) g_origFillRgba(x, y, width, height, r, g, b, a);
}

void __cdecl Hook_TriBegin(int primitiveCode) {
    if (g_hudFbo.IsCapturing()) g_hudFbo.PauseCapture();
    if (g_origTriBegin) g_origTriBegin(primitiveCode);
}

void __cdecl Hook_TriEnd() {
    if (g_origTriEnd) g_origTriEnd();
    if (g_hudFbo.IsCapturing()) g_hudFbo.ResumeCapture();
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
        if (!hookMsg("Battery",    g_origMsgBattery,    (void*)Hook_MsgFunc_Battery) &&
            !hookMsg("Health",     g_origMsgHealth,     (void*)Hook_MsgFunc_Health) &&
            !hookMsg("Damage",     g_origMsgDamage,     (void*)Hook_MsgFunc_Damage) &&
            !hookMsg("WeapPickup", g_origMsgWeapPickup, (void*)Hook_MsgFunc_WeapPickup)) {
            hookMsg("Flashlight",  g_origMsgFlashlight, (void*)Hook_MsgFunc_Flashlight);
        }
    }
    return g_origHookUserMsg ? g_origHookUserMsg(szMsgName, pfn) : 0;
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
    if (!g_engfuncs) g_engfuncs = eng;
    PatchEngSlot(eng->pfnTextMessageGet, g_origTextMsgGet, Hook_TextMessageGet);
    PatchEngSlot(eng->pfnHookUserMsg,    g_origHookUserMsg, (int(__cdecl*)(const char*, void*))Hook_pfnHookUserMsg);
    if (eng->pEfxAPI && eng->pEfxAPI->CL_AllocDlight && !g_origAllocDlight) {
        void* pAlloc = reinterpret_cast<void*>(eng->pEfxAPI->CL_AllocDlight);
        if (void* orig = hook::InstallInlineHook(pAlloc, (void*)Hook_CL_AllocDlight)) {
            g_origAllocDlight = reinterpret_cast<FnAllocDlight>(orig);
            LogMsg("[gl-flash] Hooked CL_AllocDlight at %p -> %p", pAlloc, orig);
        }
    }
    if (g_cfg.hud_compact_armor || g_cfg.hud_scale > 1.01f) {
        PatchEngSlot(eng->pfnSPR_Load,         g_origSprLoad,        Hook_SPR_Load);
        PatchEngSlot(eng->pfnSPR_Set,          g_origSprSet,         Hook_SPR_Set);
        PatchEngSlot(eng->pfnSPR_Draw,         g_origSprDraw,        Hook_SPR_Draw);
        PatchEngSlot(eng->pfnSPR_DrawHoles,    g_origSprDrawHoles,   Hook_SPR_DrawHoles);
        PatchEngSlot(eng->pfnSPR_DrawAdditive, g_origSprDrawAdditive, Hook_SPR_DrawAdditive);
        PatchEngSlot(eng->pfnFillRGBA,         g_origFillRgba,       Hook_FillRGBA);
        if (eng->pTriAPI) {
            PatchEngSlot(eng->pTriAPI->Begin, g_origTriBegin, Hook_TriBegin);
            PatchEngSlot(eng->pTriAPI->End,   g_origTriEnd,   Hook_TriEnd);
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
        if (gd && *gd && subs::Config::Lower(gd) != subs::Config::Lower(g_activeGameDir)) {
            g_activeGameDir = gd;
            ReloadAllFromDisk(true);
        }
    }
    HookClientEngfuncs(pEnginefuncs);
    int r = g_origInitialize ? g_origInitialize(pEnginefuncs, iVersion) : 0;
    g_engfuncs = pEnginefuncs;
    if (pEnginefuncs) {
        if (pEnginefuncs->pfnTextMessageGet && !g_origTextMsgGet &&
            pEnginefuncs->pfnTextMessageGet != Hook_TextMessageGet) {
            g_origTextMsgGet = reinterpret_cast<FnTextMsgGet>(
                hook::InstallInlineHook((void*)pEnginefuncs->pfnTextMessageGet, (void*)Hook_TextMessageGet));
            LogMsg("[client] Hooked pfnTextMessageGet at %p -> %p",
                   (void*)pEnginefuncs->pfnTextMessageGet, (void*)g_origTextMsgGet);
        }
        if (HMODULE cl = GetModuleHandleA("client.dll")) {
            if (cl_enginefunc_t* clientEng = FindClientEngfuncs(cl, (void*)GetProcAddress(cl, "Initialize"))) {
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
    AcquireSRWLockExclusive(&g_lock);
    ClearCaptionBuffersLocked();
    ResetHevStateLocked();
    g_clockPrevTime = -1.0f;
    g_lastClientTime = 0.0f;
    g_resetRequested = false;
    g_hasViewOrg = false;
    ReleaseSRWLockExclusive(&g_lock);
    LogMsg("[load] Host_Loadgame_f cleared caption buffers");
    if (g_origLoadgame) g_origLoadgame();
}

CRITICAL_SECTION g_hookCs;
bool g_hookCsInit = false;

struct HookGuard {
    HookGuard() { if (g_hookCsInit) EnterCriticalSection(&g_hookCs); }
    ~HookGuard() { if (g_hookCsInit) LeaveCriticalSection(&g_hookCs); }
};

void TryHookHw(HMODULE hw) {
    HookGuard guard;
    if (g_hwHooked || !hw) return;
    void* dyn  = hook::FindFunctionByStringXref(hw, "S_StartDynamicSound: %s volume > 255");
    void* stat = hook::FindFunctionByStringXref(hw, "S_StartStaticSound: %s volume > 255");
    void* load = hook::FindFunctionByStringXref(hw, "Loading game from");
    if (dyn)  g_origDynSound  = reinterpret_cast<FnStartSound>(hook::InstallInlineHook(dyn, (void*)Hook_S_StartDynamicSound));
    if (stat) g_origStatSound = reinterpret_cast<FnStartSound>(hook::InstallInlineHook(stat, (void*)Hook_S_StartStaticSound));
    if (load) g_origLoadgame  = reinterpret_cast<FnLoadgame>(hook::InstallInlineHook(load, (void*)Hook_Host_Loadgame_f));
    g_hwHooked = (g_origDynSound != nullptr || g_origStatSound != nullptr);
    LogMsg("[hw] dyn=%p->%p stat=%p->%p load=%p->%p",
           dyn, (void*)g_origDynSound, stat, (void*)g_origStatSound, load, (void*)g_origLoadgame);
}

void TryHookClient(HMODULE cl) {
    HookGuard guard;
    if (!cl) return;
    void* pInit    = (void*)GetProcAddress(cl, "Initialize");
    void* pRedraw  = (void*)GetProcAddress(cl, "HUD_Redraw");
    if (g_clientHooked && pRedraw && *static_cast<const uint8_t*>(pRedraw) == 0xE9) return;
    // Reset ONLY pointers residing inside client.dll (hw.dll engine function pointers
    // like g_origHookUserMsg / g_origTextMsgGet / g_origSpr* persist
    // across client.dll reloads because hw.dll stays loaded and already has its slots patched).
    g_origInitialize = nullptr;
    g_origHudRedraw  = nullptr;
    g_origHudVidInit = nullptr;
    g_origCalcRefdef = nullptr;
    g_origHudReset   = nullptr;
    g_origMsgBattery = nullptr;
    g_origMsgHealth = nullptr;
    g_origMsgDamage = nullptr;
    g_origMsgWeapPickup = nullptr;
    g_origMsgFlashlight = nullptr;
    g_hudSprHandles.clear();
    g_curSprIsHud = false;
    void* pCalc   = (void*)GetProcAddress(cl, "V_CalcRefdef");
    void* pReset  = (void*)GetProcAddress(cl, "HUD_Reset");
    cl_enginefunc_t* clientEng = FindClientEngfuncs(cl, pInit);
    if (clientEng && clientEng->pfnGetScreenInfo) {
        HookClientEngfuncs(clientEng);
    }
    if (pInit)    g_origInitialize = reinterpret_cast<FnInitialize>(hook::InstallInlineHook(pInit, (void*)Hook_Initialize));
    if (pRedraw)  g_origHudRedraw  = reinterpret_cast<FnHudRedraw>(hook::InstallInlineHook(pRedraw, (void*)Hook_HUD_Redraw));
    if (pCalc)   g_origCalcRefdef = reinterpret_cast<FnCalcRefdef>(hook::InstallInlineHook(pCalc, (void*)Hook_V_CalcRefdef));
    if (pReset)  g_origHudReset   = reinterpret_cast<FnHudReset>(hook::InstallInlineHook(pReset, (void*)Hook_HUD_Reset));
    g_clientHooked = (g_origInitialize != nullptr && g_origHudRedraw != nullptr);
    LogMsg("[client] mod=%p init=%p->%p redraw=%p->%p calc=%p->%p reset=%p->%p clientEng=%p",
           (void*)cl, pInit, (void*)g_origInitialize, pRedraw, (void*)g_origHudRedraw,
           pCalc, (void*)g_origCalcRefdef, pReset, (void*)g_origHudReset, (void*)clientEng);
}

void TryHookOpenGL(HMODULE gl) {
    HookGuard guard;
    if (g_glHooked || !gl) return;
    void* pSwap = (void*)GetProcAddress(gl, "wglSwapBuffers");
    if (pSwap) {
        g_origWglSwapBuffers = reinterpret_cast<FnWglSwapBuffers>(
            hook::InstallInlineHook(pSwap, (void*)Hook_WglSwapBuffers));
        g_glHooked = (g_origWglSwapBuffers != nullptr);
    }
    LogMsg("[gl] wglSwapBuffers=%p->%p", pSwap, (void*)g_origWglSwapBuffers);
}

HMODULE WINAPI Hook_LoadLibraryA(LPCSTR lpLibFileName) {
    HMODULE m = g_origLoadLibraryA ? g_origLoadLibraryA(lpLibFileName) : nullptr;
    if (m && lpLibFileName) {
        std::string s = subs::Config::Lower(lpLibFileName);
        if (s.contains("hw.dll")) TryHookHw(m);
        else if (s.contains("client.dll")) TryHookClient(m);
        else if (s.contains("opengl32.dll")) TryHookOpenGL(m);
    }
    return m;
}

void InitMod() {
    InitializeCriticalSection(&g_hookCs);
    g_hookCsInit = true;
    char exePath[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string ep = exePath;
    size_t sl = ep.find_last_of("\\/");
    g_exeDir = (sl != std::string::npos) ? ep.substr(0, sl) : ".";
    g_iniPath = g_exeDir + "\\subtitles.ini";
    g_activeGameDir = ParseCmdlineGameDir();

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExA(g_iniPath.c_str(), GetFileExInfoStandard, &fad)) {
        g_iniMtime = fad.ftLastWriteTime;
    }
    ReloadAllFromDisk(true);

    if (HMODULE hw = GetModuleHandleA("hw.dll")) TryHookHw(hw);
    if (HMODULE cl = GetModuleHandleA("client.dll")) TryHookClient(cl);
    if (HMODULE gl = GetModuleHandleA("opengl32.dll")) TryHookOpenGL(gl);
    if (HMODULE k32 = GetModuleHandleA("kernel32.dll")) {
        void* pLL = (void*)GetProcAddress(k32, "LoadLibraryA");
        if (pLL) g_origLoadLibraryA = reinterpret_cast<FnLoadLibraryA>(hook::InstallInlineHook(pLL, (void*)Hook_LoadLibraryA));
    }
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        InitMod();
    }
    return TRUE;
}
