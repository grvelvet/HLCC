#pragma once
#include <cstdint>

struct sfx_t {
    char name[64];
};

struct SCREENINFO {
    int iSize;
    int iWidth;
    int iHeight;
    int iFlags;
    int iCharHeight;
    short charWidths[256];
};

struct client_textmessage_t {
    int effect;
    uint8_t r1, g1, b1, a1;
    uint8_t r2, g2, b2, a2;
    float x;
    float y;
    float fadein;
    float fadeout;
    float holdtime;
    float fxtime;
    const char* pName;
    const char* pMessage;
};

struct pmtrace_t {
    int allsolid;
    int startsolid;
    int inopen;
    int inwater;
    float fraction;
    float endpos[3];
    float plane_normal[3];
    float plane_dist;
    int ent;
    float deltavelocity[3];
    int hitgroup;
};

struct ref_params_t {
    float vieworg[3];
    float viewangles[3];
    // Remaining fields omitted
};

// GoldSrc event_api_t (offsets verified against hw.dll: Push=0x2c, Pop=0x30, Solid=0x34, Hull=0x38, Trace=0x3c)
struct event_api_t {
    int version;                        // 0x00
    void* _pad04[10];                   // 0x04..0x28 (10 entries)
    void (*EV_PushPMStates)(void);      // 0x2c
    void (*EV_PopPMStates)(void);       // 0x30
    void (*EV_SetSolidPlayers)(int playernum); // 0x34
    void (*EV_SetTraceHull)(int hull);  // 0x38
    void (*EV_PlayerTrace)(float* start, float* end, int traceFlags, int ignore_pe, pmtrace_t* tr); // 0x3c
};

struct dlight_t {
    float origin[3];
    float radius;
    uint8_t color[3];
    uint8_t pad;
    float die;
    float decay;
    float minlight;
    int key;
    int dark;
};

// Partial GoldSrc efx_api_t (CL_AllocDlight verified at slot 52 / offset 0xD0)
struct efx_api_t {
    void* _pad00[52];
    dlight_t* (*CL_AllocDlight)(int key); // 0xD0
};

// Standard GoldSrc cl_enginefunc_t (partial up to pEventAPI)
struct triangleapi_t {
    int   version;
    void  (*RenderMode)(int mode);
    void  (*Begin)(int primitiveCode);
    void  (*End)(void);
    void  (*Color4f)(float r, float g, float b, float a);
    void  (*Color4ub)(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
    void  (*TexCoord2f)(float s, float t);
    void  (*Vertex3fv)(float* worldPnt);
    void  (*Vertex3f)(float x, float y, float z);
};

struct cl_enginefunc_t {
    int   (*pfnSPR_Load)(const char* szPicName);
    int   (*pfnSPR_Frames)(int hPic);
    int   (*pfnSPR_Height)(int hPic, int frame);
    int   (*pfnSPR_Width)(int hPic, int frame);
    void  (*pfnSPR_Set)(int hPic, int r, int g, int b);
    void  (*pfnSPR_Draw)(int frame, int x, int y, const void* prc);
    void  (*pfnSPR_DrawHoles)(int frame, int x, int y, const void* prc);
    void  (*pfnSPR_DrawAdditive)(int frame, int x, int y, const void* prc);
    void  (*pfnSPR_EnableScissor)(int x, int y, int width, int height);
    void  (*pfnSPR_DisableScissor)(void);
    void* (*pfnSPR_GetList)(char* psz, int* piCount);
    void  (*pfnFillRGBA)(int x, int y, int width, int height, int r, int g, int b, int a);
    int   (*pfnGetScreenInfo)(SCREENINFO* pscrinfo);
    void  (*pfnSetCrosshair)(int hspr, void* rc, int r, int g, int b);
    void* (*pfnRegisterVariable)(const char* szName, const char* szValue, int flags);
    float (*pfnGetCvarFloat)(const char* szName);
    char* (*pfnGetCvarString)(const char* szName);
    int   (*pfnAddCommand)(const char* cmd_name, void (*function)(void));
    int   (*pfnHookUserMsg)(const char* szMsgName, void* pfn);
    int   (*pfnServerCmd)(const char* szCmdString);
    int   (*pfnClientCmd)(const char* szCmdString);
    void  (*pfnGetPlayerInfo)(int ent_num, void* pinfo);
    void  (*pfnPlaySoundByName)(const char* szSound, float volume);
    void  (*pfnPlaySoundByIndex)(int iSound, float volume);
    void  (*pfnAngleVectors)(const float* vecAngles, float* forward, float* right, float* up);
    client_textmessage_t* (*pfnTextMessageGet)(const char* pName);
    int   (*pfnDrawCharacter)(int x, int y, int number, int r, int g, int b);
    int   (*pfnDrawConsoleString)(int x, int y, char* string);
    void  (*pfnDrawSetTextColor)(float r, float g, float b);
    void  (*pfnDrawConsoleStringLen)(const char* string, int* length, int* height);
    void  (*pfnConsolePrint)(const char* string);
    void  (*pfnCenterPrint)(const char* string);
    int   (*GetWindowCenterX)(void);
    int   (*GetWindowCenterY)(void);
    void  (*GetViewAngles)(float*);
    void  (*SetViewAngles)(float*);
    int   (*GetMaxClients)(void);
    void  (*Cvar_SetValue)(const char* cvar, float value);
    int   (*Cmd_Argc)(void);
    char* (*Cmd_Argv)(int arg);
    void  (*Con_Printf)(const char* fmt, ...);
    void  (*Con_DPrintf)(const char* fmt, ...);
    void  (*Con_NPrintf)(int pos, const char* fmt, ...);
    void  (*Con_NXPrintf)(void* info, const char* fmt, ...);
    const char* (*PhysInfo_ValueForKey)(const char* key);
    const char* (*ServerInfo_ValueForKey)(const char* key);
    float (*GetClientMaxspeed)(void);
    int   (*CheckParm)(const char* parm, char** ppnext);
    void  (*Key_Event)(int key, int down);
    void  (*GetMousePosition)(int* mx, int* my);
    int   (*IsNoClipping)(void);
    void* (*GetLocalPlayer)(void);
    void* (*GetViewModel)(void);
    void* (*GetEntityByIndex)(int idx);
    float (*GetClientTime)(void);
    void  (*V_CalcShake)(void);
    void  (*V_ApplyShake)(float* origin, float* angles, float factor);
    int   (*PM_PointContents)(float* point, int* truecontents);
    int   (*PM_WaterEntity)(float* p);
    void* (*PM_TraceLine)(float* start, float* end, int flags, int usehull, int ignore_pe);
    void* (*CL_LoadModel)(const char* modelname, int* index);
    int   (*CL_CreateVisibleEntity)(int type, void* ent);
    const void* (*GetSpritePointer)(int hSprite);
    void  (*pfnPlaySoundByNameAtLocation)(char* szSound, float volume, float* origin);
    unsigned short (*pfnPrecacheEvent)(int type, const char* psz);
    void  (*pfnPlaybackEvent)(int flags, const void* pInvoker, unsigned short eventindex, float delay, float* origin, float* angles, float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2);
    void  (*pfnWeaponAnim)(int iAnim, int body);
    float (*pfnRandomFloat)(float low, float high);
    long  (*pfnRandomLong)(long low, long high);
    void  (*pfnHookEvent)(const char* name, void (*pfnEvent)(void* args));
    int   (*Con_IsVisible)(void);
    const char* (*pfnGetGameDirectory)(void);
    void* (*pfnGetCvarPointer)(const char* szName);
    const char* (*Key_LookupBinding)(const char* pBinding);
    const char* (*pfnGetLevelName)(void);
    void  (*pfnGetScreenFade)(void* fade);
    void  (*pfnSetScreenFade)(void* fade);
    void* (*VGui_GetPanel)(void);
    void  (*VGui_ViewportPaintBackground)(int extents[4]);
    uint8_t* (*COM_LoadFile)(const char* path, int usehunk, int* pLength);
    char* (*COM_ParseFile)(char* data, char* token);
    void  (*COM_FreeFile)(void* buffer);
    triangleapi_t* pTriAPI;
    efx_api_t* pEfxAPI;
    event_api_t* pEventAPI;
};

// VGUI_Surface026 vtable wrapper (slots verified against vgui2.dll / version.dll.bak)
struct VGuiSurface {
    template <typename Fn>
    Fn Slot(size_t idx) const {
        auto vt = *reinterpret_cast<void* const* const*>(this);
        return reinterpret_cast<Fn>(vt[idx]);
    }
    unsigned int GetEmbeddedPanel() {
        using Fn = unsigned int(__thiscall*)(void*);
        return Slot<Fn>(3)(this);
    }
    void DrawSetColor(int r, int g, int b, int a) {
        using Fn = void(__thiscall*)(void*, int, int, int, int);
        Slot<Fn>(10)(this, r, g, b, a);
    }
    void DrawFilledRect(int x0, int y0, int x1, int y1) {
        using Fn = void(__thiscall*)(void*, int, int, int, int);
        Slot<Fn>(12)(this, x0, y0, x1, y1);
    }
    void DrawSetTextFont(unsigned long font) {
        using Fn = void(__thiscall*)(void*, unsigned long);
        Slot<Fn>(13)(this, font);
    }
    void DrawSetTextColor(int r, int g, int b, int a) {
        using Fn = void(__thiscall*)(void*, int, int, int, int);
        Slot<Fn>(15)(this, r, g, b, a);
    }
    void DrawSetTextPos(int x, int y) {
        using Fn = void(__thiscall*)(void*, int, int);
        Slot<Fn>(16)(this, x, y);
    }
    void DrawPrintText(const wchar_t* text, int textLen) {
        using Fn = void(__thiscall*)(void*, const wchar_t*, int);
        Slot<Fn>(18)(this, text, textLen);
    }
    void* PaintTraversePtr() const {
        auto vt = *reinterpret_cast<void* const* const*>(this);
        return vt[78];
    }
    unsigned long CreateFont() {
        using Fn = unsigned long(__thiscall*)(void*);
        return Slot<Fn>(59)(this);
    }
    bool AddGlyphSetToFont(unsigned long font, const char* windowsFontName, int tall, int weight,
                           int blur, int scanlines, int flags, int nRangeMin = 0, int nRangeMax = 0) {
        using Fn = bool(__thiscall*)(void*, unsigned long, const char*, int, int, int, int, int, int, int);
        return Slot<Fn>(60)(this, font, windowsFontName, tall, weight, blur, scanlines, flags, nRangeMin, nRangeMax);
    }
    int GetFontTall(unsigned long font) {
        using Fn = int(__thiscall*)(void*, unsigned long);
        return Slot<Fn>(62)(this, font);
    }
    void GetTextSize(unsigned long font, const wchar_t* text, int& wide, int& tall) {
        using Fn = void(__thiscall*)(void*, unsigned long, const wchar_t*, int&, int&);
        Slot<Fn>(65)(this, font, text, wide, tall);
    }
};
