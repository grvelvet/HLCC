#include "core/text_layout.hpp"

#include <cwctype>

namespace core {

std::wstring TrimWs(const std::wstring& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) --b;
    return s.substr(a, b - a);
}

std::vector<std::wstring> SplitSentenceClauses(const std::wstring& s) {
    std::vector<std::wstring> out;
    size_t start = 0;
    for (size_t i = 0; i + 1 < s.size(); ++i) {
        wchar_t c = s[i];
        bool endMark = (c == L'!' || c == L'?' || c == L'\u2026');
        bool dotBreak = (c == L'.' && s[i + 1] == L' ' && i >= 2 && s[i - 1] != L'.');
        if ((endMark && s[i + 1] == L' ') || dotBreak) {
            // Avoid splitting abbreviations like "д.ф.н." where next char is lowercase.
            if (dotBreak && i + 2 < s.size() && std::iswlower(s[i + 2])) continue;
            std::wstring part = TrimWs(s.substr(start, i - start + 1));
            if (!part.empty()) out.push_back(std::move(part));
            start = i + 2;
        }
    }
    std::wstring tail = TrimWs(s.substr(start));
    if (!tail.empty()) out.push_back(std::move(tail));
    return out;
}

} // namespace core
