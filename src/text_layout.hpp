#pragma once

/**
 * @file text_layout.hpp
 * @brief Word-wrapping and sentence-aware page splitting for long subtitle lines.
 *
 * Used by default style to paginate multi-line captions cleanly at clause
 * boundaries (||, periods, semicolons) without mid-sentence jumps.
 */

#include "app_state.hpp"

template <typename MeasureFn>
std::vector<std::wstring> WrapTextWith(const std::wstring& text, int maxW, const MeasureFn& measureWidth) {
    std::vector<std::wstring> lines;
    std::wstring cur;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == L'\n') {
            lines.push_back(cur);
            cur.clear();
            i++;
            continue;
        }
        size_t next = text.find_first_of(L" \n", i);
        if (next == std::wstring::npos) next = text.size();
        std::wstring word = text.substr(i, next - i);
        std::wstring cand = cur.empty() ? word : (cur + L" " + word);
        int w = measureWidth(cand);
        if (!cur.empty() && w > maxW) {
            lines.push_back(cur);
            cur = word;
        } else {
            cur = cand;
        }
        i = (next < text.size() && text[next] == L' ') ? (next + 1) : next;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

std::wstring TrimWs(const std::wstring& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) --b;
    return s.substr(a, b - a);
}

// Split a body segment on sentence boundaries (. ! ? …) into trimmed clauses.
std::vector<std::wstring> SplitSentenceClauses(const std::wstring& s) {
    std::vector<std::wstring> out;
    size_t start = 0;
    for (size_t i = 0; i + 1 < s.size(); ++i) {
        wchar_t c = s[i];
        bool endMark = (c == L'!' || c == L'?' || c == L'\u2026');
        bool dotBreak = (c == L'.' && s[i + 1] == L' ' && i >= 2 && s[i - 1] != L'.');
        if ((endMark && s[i + 1] == L' ') || dotBreak) {
            // Avoid splitting abbreviations like "д.ф.н." where next char is lowercase.
            if (dotBreak && i + 2 < s.size() && iswlower(s[i + 2])) continue;
            std::wstring part = TrimWs(s.substr(start, i - start + 1));
            if (!part.empty()) out.push_back(std::move(part));
            start = i + 2;
        }
    }
    std::wstring tail = TrimWs(s.substr(start));
    if (!tail.empty()) out.push_back(std::move(tail));
    return out;
}

template <typename MeasureFn>
void AppendSubClausesFitting(const std::wstring& prefix, const std::wstring& clause,
                             int maxLines, int maxW, const MeasureFn& measureWidth,
                             std::vector<std::wstring>& out) {
    if (WrapTextWith(prefix + clause, maxW, measureWidth).size() <= (size_t)maxLines) {
        out.push_back(clause);
        return;
    }
    // Try splitting on comma / semicolon / em-dash first.
    std::vector<std::wstring> pieces;
    size_t start = 0;
    for (size_t i = 0; i + 1 < clause.size(); ++i) {
        if ((clause[i] == L',' || clause[i] == L';') && clause[i + 1] == L' ') {
            std::wstring p = TrimWs(clause.substr(start, i - start + 1));
            if (!p.empty()) pieces.push_back(std::move(p));
            start = i + 2;
        } else if (clause[i] == L'\u2014' && i > 0 && clause[i - 1] == L' ' && clause[i + 1] == L' ') {
            std::wstring p = TrimWs(clause.substr(start, i - start + 1));
            if (!p.empty()) pieces.push_back(std::move(p));
            start = i + 2;
        }
    }
    std::wstring tail = TrimWs(clause.substr(start));
    if (!tail.empty()) pieces.push_back(std::move(tail));

    if (pieces.size() > 1) {
        for (const auto& p : pieces) {
            if (WrapTextWith(prefix + p, maxW, measureWidth).size() <= (size_t)maxLines) {
                out.push_back(p);
            } else {
                // Fallback: split by words into maxLines chunks.
                std::vector<std::wstring> lines = WrapTextWith(prefix + p, maxW, measureWidth);
                if (!prefix.empty() && !lines.empty() && lines[0].rfind(prefix, 0) == 0) {
                    lines[0] = lines[0].substr(prefix.size());
                }
                for (size_t i = 0; i < lines.size(); i += (size_t)maxLines) {
                    std::wstring chunk;
                    for (size_t j = i; j < std::min(lines.size(), i + (size_t)maxLines); ++j) {
                        if (!chunk.empty()) chunk += L' ';
                        chunk += TrimWs(lines[j]);
                    }
                    if (!chunk.empty()) out.push_back(std::move(chunk));
                }
            }
        }
        return;
    }

    // Fallback: word-level chunking.
    std::vector<std::wstring> lines = WrapTextWith(prefix + clause, maxW, measureWidth);
    if (!prefix.empty() && !lines.empty() && lines[0].rfind(prefix, 0) == 0) {
        lines[0] = lines[0].substr(prefix.size());
    }
    for (size_t i = 0; i < lines.size(); i += (size_t)maxLines) {
        std::wstring chunk;
        for (size_t j = i; j < std::min(lines.size(), i + (size_t)maxLines); ++j) {
            if (!chunk.empty()) chunk += L' ';
            chunk += TrimWs(lines[j]);
        }
        if (!chunk.empty()) out.push_back(std::move(chunk));
    }
}

template <typename MeasureFn>
std::vector<CaptionPage> BuildCaptionPages(const std::wstring& fullText, ULONGLONG startMs, ULONGLONG expireMs,
                                           int maxLinesPerCap, int maxW, const MeasureFn& measureWidth) {
    bool hasManualSplit = fullText.contains(L"||");
    if (!hasManualSplit && (maxLinesPerCap <= 0 ||
        WrapTextWith(fullText, maxW, measureWidth).size() <= (size_t)maxLinesPerCap)) {
        return {{fullText, startMs, expireMs}};
    }

    std::wstring prefix;
    std::wstring body = fullText;
    if (!fullText.empty() && fullText[0] == L'[') {
        size_t close = fullText.find(L']');
        if (close != std::wstring::npos && close + 1 < fullText.size() && fullText[close + 1] == L' ') {
            prefix = fullText.substr(0, close + 2);
            body = fullText.substr(close + 2);
        }
    }

    // Split on manual "||" first, then split oversized groups into sentence/clause pages.
    std::vector<std::wstring> manualGroups;
    for (auto part : std::wstring_view(body) | std::views::split(std::wstring_view(L"||"))) {
        std::wstring g = TrimWs(std::wstring(part.begin(), part.end()));
        if (!g.empty()) manualGroups.push_back(std::move(g));
    }

    std::vector<std::wstring> pageBodies;
    int limit = std::max(1, maxLinesPerCap);
    for (const auto& grp : manualGroups) {
        if (maxLinesPerCap <= 0 || WrapTextWith(prefix + grp, maxW, measureWidth).size() <= (size_t)limit) {
            pageBodies.push_back(grp);
            continue;
        }
        std::vector<std::wstring> clauses;
        for (const auto& sc : SplitSentenceClauses(grp)) {
            AppendSubClausesFitting(prefix, sc, limit, maxW, measureWidth, clauses);
        }
        std::wstring cur;
        for (const auto& cl : clauses) {
            std::wstring cand = cur.empty() ? cl : (cur + L" " + cl);
            if (!cur.empty() && WrapTextWith(prefix + cand, maxW, measureWidth).size() > (size_t)limit) {
                pageBodies.push_back(cur);
                cur = cl;
            } else {
                cur = std::move(cand);
            }
        }
        if (!cur.empty()) pageBodies.push_back(std::move(cur));
    }

    if (pageBodies.empty()) return {{fullText, startMs, expireMs}};

    ULONGLONG totalDur = (expireMs > startMs) ? (expireMs - startMs) : 1200ULL;
    size_t totalWeight = 0;
    for (const auto& pb : pageBodies) totalWeight += std::max<size_t>(1, pb.size());

    std::vector<CaptionPage> pages;
    pages.reserve(pageBodies.size());
    ULONGLONG cursor = startMs;
    for (size_t i = 0; i < pageBodies.size(); ++i) {
        ULONGLONG endMs = expireMs;
        if (i + 1 < pageBodies.size()) {
            size_t w = std::max<size_t>(1, pageBodies[i].size());
            ULONGLONG dur = (totalDur * w) / totalWeight;
            if (totalDur >= (ULONGLONG)pageBodies.size() * 1500ULL) {
                dur = std::max<ULONGLONG>(dur, 1500ULL);
            }
            ULONGLONG remainingMin = (totalDur >= (ULONGLONG)pageBodies.size() * 1200ULL)
                                     ? (ULONGLONG)(pageBodies.size() - 1 - i) * 1200ULL : 0ULL;
            if (cursor + dur + remainingMin > expireMs && expireMs > cursor + remainingMin) {
                dur = expireMs - cursor - remainingMin;
            }
            endMs = std::min(expireMs, cursor + std::max<ULONGLONG>(400ULL, dur));
        }
        pages.push_back({prefix + pageBodies[i], cursor, endMs});
        cursor = endMs;
    }
    return pages;
}

template <typename MeasureFn>
const std::wstring& GetActiveCaptionText(ActiveCaption& cap, const MeasureFn& measureWidth,
                                          ULONGLONG nowMs, int wrapW, int maxLinesPerCap) {
    if (cap.pages.empty() || cap.cachedWrapW != wrapW || cap.cachedMaxLines != maxLinesPerCap) {
        cap.pages = BuildCaptionPages(cap.text, cap.start_ms, cap.expire_ms, maxLinesPerCap, wrapW, measureWidth);
        cap.cachedWrapW = wrapW;
        cap.cachedMaxLines = maxLinesPerCap;
    }
    for (const auto& p : cap.pages) {
        if (nowMs < p.end_ms) return p.text;
    }
    return cap.pages.back().text;
}

