#include <windows.h>

static HMODULE g_sysVersion = nullptr;

static HMODULE GetSysVersion() {
    if (!g_sysVersion) {
        char sysDir[MAX_PATH];
        UINT len = GetSystemDirectoryA(sysDir, MAX_PATH);
        if (len > 0 && len + 13 < MAX_PATH) {
            lstrcatA(sysDir, "\\version.dll");
            g_sysVersion = LoadLibraryA(sysDir);
        }
    }
    return g_sysVersion;
}

static FARPROC GetSysProc(const char* name) {
    HMODULE m = GetSysVersion();
    return m ? GetProcAddress(m, name) : nullptr;
}

extern "C" {

BOOL WINAPI proxy_GetFileVersionInfoA(LPCSTR a, DWORD b, DWORD c, LPVOID d) {
    using Fn = BOOL(WINAPI*)(LPCSTR, DWORD, DWORD, LPVOID);
    auto fn = reinterpret_cast<Fn>(GetSysProc("GetFileVersionInfoA"));
    return fn ? fn(a, b, c, d) : FALSE;
}

int WINAPI proxy_GetFileVersionInfoByHandle(int a, HANDLE b, int c, int d) {
    using Fn = int(WINAPI*)(int, HANDLE, int, int);
    auto fn = reinterpret_cast<Fn>(GetSysProc("GetFileVersionInfoByHandle"));
    return fn ? fn(a, b, c, d) : 0;
}

BOOL WINAPI proxy_GetFileVersionInfoExA(DWORD a, LPCSTR b, DWORD c, DWORD d, LPVOID e) {
    using Fn = BOOL(WINAPI*)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
    auto fn = reinterpret_cast<Fn>(GetSysProc("GetFileVersionInfoExA"));
    return fn ? fn(a, b, c, d, e) : FALSE;
}

BOOL WINAPI proxy_GetFileVersionInfoExW(DWORD a, LPCWSTR b, DWORD c, DWORD d, LPVOID e) {
    using Fn = BOOL(WINAPI*)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
    auto fn = reinterpret_cast<Fn>(GetSysProc("GetFileVersionInfoExW"));
    return fn ? fn(a, b, c, d, e) : FALSE;
}

DWORD WINAPI proxy_GetFileVersionInfoSizeA(LPCSTR a, LPDWORD b) {
    using Fn = DWORD(WINAPI*)(LPCSTR, LPDWORD);
    auto fn = reinterpret_cast<Fn>(GetSysProc("GetFileVersionInfoSizeA"));
    return fn ? fn(a, b) : 0;
}

DWORD WINAPI proxy_GetFileVersionInfoSizeExA(DWORD a, LPCSTR b, LPDWORD c) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPDWORD);
    auto fn = reinterpret_cast<Fn>(GetSysProc("GetFileVersionInfoSizeExA"));
    return fn ? fn(a, b, c) : 0;
}

DWORD WINAPI proxy_GetFileVersionInfoSizeExW(DWORD a, LPCWSTR b, LPDWORD c) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPDWORD);
    auto fn = reinterpret_cast<Fn>(GetSysProc("GetFileVersionInfoSizeExW"));
    return fn ? fn(a, b, c) : 0;
}

DWORD WINAPI proxy_GetFileVersionInfoSizeW(LPCWSTR a, LPDWORD b) {
    using Fn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    auto fn = reinterpret_cast<Fn>(GetSysProc("GetFileVersionInfoSizeW"));
    return fn ? fn(a, b) : 0;
}

BOOL WINAPI proxy_GetFileVersionInfoW(LPCWSTR a, DWORD b, DWORD c, LPVOID d) {
    using Fn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    auto fn = reinterpret_cast<Fn>(GetSysProc("GetFileVersionInfoW"));
    return fn ? fn(a, b, c, d) : FALSE;
}

DWORD WINAPI proxy_VerFindFileA(DWORD a, LPCSTR b, LPCSTR c, LPCSTR d, LPSTR e, PUINT f, LPSTR g, PUINT h) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
    auto fn = reinterpret_cast<Fn>(GetSysProc("VerFindFileA"));
    return fn ? fn(a, b, c, d, e, f, g, h) : 0;
}

DWORD WINAPI proxy_VerFindFileW(DWORD a, LPCWSTR b, LPCWSTR c, LPCWSTR d, LPWSTR e, PUINT f, LPWSTR g, PUINT h) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
    auto fn = reinterpret_cast<Fn>(GetSysProc("VerFindFileW"));
    return fn ? fn(a, b, c, d, e, f, g, h) : 0;
}

DWORD WINAPI proxy_VerInstallFileA(DWORD a, LPCSTR b, LPCSTR c, LPCSTR d, LPCSTR e, LPCSTR f, LPSTR g, PUINT h) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
    auto fn = reinterpret_cast<Fn>(GetSysProc("VerInstallFileA"));
    return fn ? fn(a, b, c, d, e, f, g, h) : 0;
}

DWORD WINAPI proxy_VerInstallFileW(DWORD a, LPCWSTR b, LPCWSTR c, LPCWSTR d, LPCWSTR e, LPCWSTR f, LPWSTR g, PUINT h) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
    auto fn = reinterpret_cast<Fn>(GetSysProc("VerInstallFileW"));
    return fn ? fn(a, b, c, d, e, f, g, h) : 0;
}

DWORD WINAPI proxy_VerLanguageNameA(DWORD a, LPSTR b, DWORD c) {
    using Fn = DWORD(WINAPI*)(DWORD, LPSTR, DWORD);
    auto fn = reinterpret_cast<Fn>(GetSysProc("VerLanguageNameA"));
    return fn ? fn(a, b, c) : 0;
}

DWORD WINAPI proxy_VerLanguageNameW(DWORD a, LPWSTR b, DWORD c) {
    using Fn = DWORD(WINAPI*)(DWORD, LPWSTR, DWORD);
    auto fn = reinterpret_cast<Fn>(GetSysProc("VerLanguageNameW"));
    return fn ? fn(a, b, c) : 0;
}

BOOL WINAPI proxy_VerQueryValueA(LPCVOID a, LPCSTR b, LPVOID* c, PUINT d) {
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCSTR, LPVOID*, PUINT);
    auto fn = reinterpret_cast<Fn>(GetSysProc("VerQueryValueA"));
    return fn ? fn(a, b, c, d) : FALSE;
}

BOOL WINAPI proxy_VerQueryValueW(LPCVOID a, LPCWSTR b, LPVOID* c, PUINT d) {
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    auto fn = reinterpret_cast<Fn>(GetSysProc("VerQueryValueW"));
    return fn ? fn(a, b, c, d) : FALSE;
}

} // extern "C"
