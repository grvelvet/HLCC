#pragma once

#include <string>

namespace core {

void SetLogContext(bool enabled, const std::string& exeDir);
void LogMsg(const char* fmt, ...);
std::wstring Utf8ToWide(const std::string& s);
std::string ParseCmdlineGameDir();

} // namespace core
