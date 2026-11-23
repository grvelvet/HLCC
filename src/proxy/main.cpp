/**
 * @file main.cpp
 * @brief Thin DLL entry point for version.dll proxy.
 */

#include <windows.h>

#include "goldsrc/engine_hooks.hpp"

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        goldsrc::InitMod();
    }
    return TRUE;
}
