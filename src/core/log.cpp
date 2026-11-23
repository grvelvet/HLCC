#include "core/log.hpp"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace core {

namespace {
bool g_debugLogEnabled = false;
std::string g_logExeDir;
bool g_logOpened = false;
} // namespace

void SetLogContext(bool enabled, const std::string& exeDir) {
    g_debugLogEnabled = enabled;
    g_logExeDir = exeDir;
}

void LogMsg(const char* fmt, ...) {
    if (!g_debugLogEnabled || g_logExeDir.empty()) return;
    std::string logPath = g_logExeDir + "\\subtitles_debug.log";
    FILE* f = std::fopen(logPath.c_str(), g_logOpened ? "a" : "w");
    if (!f) return;
    g_logOpened = true;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string ParseCmdlineGameDir() {
    const char* cmd = GetCommandLineA();
    if (!cmd) return "valve";
    const char* p = cmd;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (_strnicmp(p, "-game", 5) == 0 && (p[5] == ' ' || p[5] == '\t')) {
            p += 5;
            while (*p == ' ' || *p == '\t') p++;
            bool q = (*p == '"');
            if (q) p++;
            std::string val;
            while (*p && (q ? (*p != '"') : (*p != ' ' && *p != '\t'))) {
                val.push_back(*p++);
            }
            if (!val.empty()) return val;
        }
        if (*p == '"') {
            p++;
            while (*p && *p != '"') p++;
            if (*p == '"') p++;
        } else {
            while (*p && *p != ' ' && *p != '\t') p++;
        }
    }
    return "valve";
}

} // namespace core
