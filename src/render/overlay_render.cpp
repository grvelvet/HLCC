#include "render/overlay_render.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "core/log.hpp"
#include "core/text_layout.hpp"

namespace render {

void OverlayRenderer::InvalidateFonts() noexcept {
    atlas_.Invalidate();
    voxAtlas_.Invalidate();
    speakerAtlas_.Invalidate();
    titleAtlas_.Invalidate();
}

void OverlayRenderer::DrawActiveTitles(glfont::GlApi& gl, core::CaptionState& state,
                                       int screenW, int screenH, int fontH) {
    ULONGLONG nowMs = GetTickCount64();
    int lineStep = fontH + 4;
    for (size_t idx = 0; idx < state.activeTitles.size(); ) {
        const auto& at = state.activeTitles[idx];
        ULONGLONG elapsed = (nowMs >= at.start_ms) ? (nowMs - at.start_ms) : 0;
        ULONGLONG total = at.fadein_ms + at.hold_ms + at.fadeout_ms;
        if (elapsed >= total) {
            state.activeTitles.erase(state.activeTitles.begin() + static_cast<ptrdiff_t>(idx));
            continue;
        }
        idx++;

        float alphaF = 1.0f;
        if (at.fadein_ms > 0 && elapsed < at.fadein_ms) {
            alphaF = static_cast<float>(elapsed) / static_cast<float>(at.fadein_ms);
        } else if (at.fadeout_ms > 0 && elapsed > at.fadein_ms + at.hold_ms) {
            ULONGLONG outEl = elapsed - (at.fadein_ms + at.hold_ms);
            alphaF = 1.0f - static_cast<float>(outEl) / static_cast<float>(at.fadeout_ms);
        }
        int alpha = std::clamp(static_cast<int>(alphaF * 180.0f), 0, 180);
        if (alpha <= 0) continue;

        std::vector<std::wstring> lines;
        for (auto part : std::wstring_view(at.text) | std::views::split(L'\n')) {
            lines.emplace_back(part.begin(), part.end());
        }

        int totalH = static_cast<int>(lines.size()) * lineStep;
        int baseY = (at.y < 0.0f) ? (screenH - totalH) / 2 : static_cast<int>(at.y * static_cast<float>(screenH));

        for (size_t li = 0; li < lines.size(); ++li) {
            if (lines[li].empty()) continue;
            int w = titleAtlas_.MeasureWidth(gl, lines[li]);
            int x = (at.x < 0.0f) ? (screenW - w) / 2 : static_cast<int>(at.x * static_cast<float>(screenW));
            int y = baseY + static_cast<int>(li) * lineStep;
            titleAtlas_.DrawText(gl, x, y, lines[li], 255, 255, 255, static_cast<uint8_t>(alpha), false);
        }
    }
}

void OverlayRenderer::DrawRoundedRect(glfont::GlApi& gl, int x0, int y0, int x1, int y1, int radius,
                                      uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x1 <= x0 || y1 <= y0 || a == 0) return;
    int rad = std::max(0, std::min({radius, (x1 - x0) / 2, (y1 - y0) / 2}));
    gl.glDisable(glfont::GlApi::kTexture2D);
    gl.glColor4ub(r, g, b, a);
    gl.glBegin(glfont::GlApi::kQuads);
    auto emitQuad = [&](int qx0, int qy0, int qx1, int qy1) {
        if (qx1 <= qx0 || qy1 <= qy0) return;
        gl.glVertex2f(static_cast<float>(qx0), static_cast<float>(qy0));
        gl.glVertex2f(static_cast<float>(qx1), static_cast<float>(qy0));
        gl.glVertex2f(static_cast<float>(qx1), static_cast<float>(qy1));
        gl.glVertex2f(static_cast<float>(qx0), static_cast<float>(qy1));
    };
    if (rad <= 0) {
        emitQuad(x0, y0, x1, y1);
    } else {
        for (int row = 0; row < rad; ++row) {
            double dy = static_cast<double>(rad) - static_cast<double>(row) - 0.5;
            double dx = std::sqrt(std::max(0.0, static_cast<double>(rad * rad) - dy * dy));
            int inset = std::max(0, rad - static_cast<int>(std::lround(dx)));
            emitQuad(x0 + inset, y0 + row, x1 - inset, y0 + row + 1);
            emitQuad(x0 + inset, y1 - row - 1, x1 - inset, y1 - row);
        }
        emitQuad(x0, y0 + rad, x1, y1 - rad);
    }
    gl.glEnd();
    gl.glEnable(glfont::GlApi::kTexture2D);
}

void OverlayRenderer::Render(glfont::GlApi& gl, core::CaptionState& state, int screenW, int screenH) {
    if (screenW <= 0 || screenH <= 0) return;
    if (!gl.Load()) return;
    HGLRC ctx = gl.wglGetCurrentContext();
    if (!ctx) return;

    AcquireSRWLockExclusive(&state.lock);
    if (state.IsGamePausedLocked(GetTickCount64()) || (state.captions.empty() && state.activeTitles.empty())) {
        ReleaseSRWLockExclusive(&state.lock);
        return;
    }

    const auto& cfg = state.cfg;
    int tall = (cfg.font_size > 0) ? cfg.font_size : std::max(14, (screenH * 20) / 1080);
    std::wstring face = core::Utf8ToWide(cfg.font_name);
    if (face.empty()) face = L"Tahoma";
    if (auto res = atlas_.EnsureReady(gl, ctx, face, tall, false, false); !res) {
        core::LogMsg("[gl] atlas_ failed: %s", res.error().c_str());
        ReleaseSRWLockExclusive(&state.lock);
        return;
    }
    bool voxReady = voxAtlas_.EnsureReady(gl, ctx, L"Tahoma", tall, true, false).has_value();
    bool speakerReady = speakerAtlas_.EnsureReady(gl, ctx, face, tall, false, true).has_value();

    gl.glPushAttrib(glfont::GlApi::kAllAttribBits);
    gl.glPushClientAttrib(glfont::GlApi::kClientAllAttribBits);

    if (gl.glActiveTextureARB) {
        for (int u = 1; u <= 3; ++u) {
            gl.glActiveTextureARB(glfont::GlApi::kTexture0 + static_cast<unsigned int>(u));
            gl.glDisable(glfont::GlApi::kTexture2D);
        }
        gl.glActiveTextureARB(glfont::GlApi::kTexture0);
    }

    gl.glDisable(glfont::GlApi::kDepthTest);
    gl.glDisable(glfont::GlApi::kCullFace);
    gl.glDisable(glfont::GlApi::kScissorTest);
    gl.glDisable(glfont::GlApi::kAlphaTest);
    gl.glDisable(glfont::GlApi::kLighting);
    gl.glDisable(glfont::GlApi::kFog);
    gl.glDepthMask(0);
    gl.glEnable(glfont::GlApi::kBlend);
    gl.glBlendFunc(glfont::GlApi::kSrcAlpha, glfont::GlApi::kOneMinusSrcAlpha);
    gl.glEnable(glfont::GlApi::kTexture2D);
    gl.glTexEnvi(glfont::GlApi::kTextureEnv, glfont::GlApi::kTextureEnvMode, glfont::GlApi::kModulate);

    gl.glMatrixMode(glfont::GlApi::kProjection);
    gl.glPushMatrix();
    gl.glLoadIdentity();
    gl.glOrtho(0.0, static_cast<double>(screenW), static_cast<double>(screenH), 0.0, -1.0, 1.0);
    gl.glMatrixMode(glfont::GlApi::kModelview);
    gl.glPushMatrix();
    gl.glLoadIdentity();
    gl.glMatrixMode(glfont::GlApi::kTexture);
    gl.glPushMatrix();
    gl.glLoadIdentity();

    int fontH = atlas_.LineHeight();

    if (!state.activeTitles.empty()) {
        int titleTall = std::max(16, (screenH * 24) / 1080);
        if (titleAtlas_.EnsureReady(gl, ctx, L"Trebuchet MS", titleTall, false, false).has_value()) {
            DrawActiveTitles(gl, state, screenW, screenH, titleAtlas_.LineHeight());
        }
    }

    if (!state.captions.empty()) {
        const bool isHl2Style = (cfg.style == "hl2");
        const int padX = 16, padY = isHl2Style ? 10 : 8;
        const int safeMargin = std::max(16, std::min(screenW, screenH) / 40);
        const int widthPct = (isHl2Style && cfg.max_width_pct == 62) ? 48 : cfg.max_width_pct;
        int maxBoxW = std::min((screenW * widthPct) / 100,
                               std::max(40, screenW - safeMargin * 2));
        int lineStep = fontH + (isHl2Style ? 4 : 3);

        struct RenderLine {
            std::wstring txt;
            subs::Rgb col;
            int w;
            uint8_t alpha;
            glfont::FontAtlas* atlas;
            bool isFirstLine;
            subs::RoleCat cat;
        };
        auto drawLineText = [&](const RenderLine& rl, int x, int y) {
            glfont::FontAtlas& a = *rl.atlas;
            const uint8_t br = isHl2Style ? static_cast<uint8_t>(rl.col.r) : 255;
            const uint8_t bg = isHl2Style ? static_cast<uint8_t>(rl.col.g) : 255;
            const uint8_t bb = isHl2Style ? static_cast<uint8_t>(rl.col.b) : 255;
            if (core::IsSfxCat(rl.cat)) {
                a.DrawText(gl, x, y, rl.txt, static_cast<uint8_t>(rl.col.r), static_cast<uint8_t>(rl.col.g),
                           static_cast<uint8_t>(rl.col.b), rl.alpha);
                return;
            }
            if (!isHl2Style && rl.isFirstLine && !rl.txt.empty() && rl.txt[0] == L'[') {
                size_t rb = rl.txt.find(L']');
                if (rb != std::wstring::npos) {
                    std::wstring speaker = rl.txt.substr(0, rb + 1);
                    std::wstring body = rl.txt.substr(rb + 1);
                    glfont::FontAtlas& spAtlas = speakerReady ? speakerAtlas_ : a;
                    spAtlas.DrawText(gl, x, y, speaker, static_cast<uint8_t>(rl.col.r),
                                     static_cast<uint8_t>(rl.col.g), static_cast<uint8_t>(rl.col.b), rl.alpha);
                    if (!body.empty()) {
                        int spW = spAtlas.MeasureWidth(gl, speaker);
                        a.DrawText(gl, x + spW, y, body, br, bg, bb, rl.alpha);
                    }
                    return;
                }
            }
            a.DrawText(gl, x, y, rl.txt, br, bg, bb, rl.alpha);
        };

        std::vector<std::vector<RenderLine>> perCap;
        perCap.reserve(state.captions.size());
        int totalLines = 0;
        ULONGLONG hl2CapElapsedMs = 0;
        ULONGLONG hl2CapDurMs = 1;
        for (size_t ci = 0; ci < state.captions.size(); ++ci) {
            auto& cap = state.captions[ci];
            float fIn = (state.gameMs >= cap.start_ms)
                ? std::clamp(static_cast<float>(state.gameMs - cap.start_ms) / 150.0f, 0.0f, 1.0f)
                : 0.0f;
            float fOut = (cap.expire_ms > state.gameMs)
                ? std::clamp(static_cast<float>(cap.expire_ms - state.gameMs) / 220.0f, 0.0f, 1.0f)
                : 0.0f;
            uint8_t capAlpha = static_cast<uint8_t>(std::lround(255.0f * std::min(fIn, fOut)));

            glfont::FontAtlas& capAtlas = (voxReady && (cap.cat == subs::RoleCat::VOX || core::IsSfxCat(cap.cat)))
                ? voxAtlas_ : atlas_;
            auto measureCap = [&](const std::wstring& s) -> int {
                if (!isHl2Style && !core::IsSfxCat(cap.cat) && !s.empty() && s[0] == L'[') {
                    size_t rb = s.find(L']');
                    if (rb != std::wstring::npos) {
                        glfont::FontAtlas& spAtlas = speakerReady ? speakerAtlas_ : capAtlas;
                        return spAtlas.MeasureWidth(gl, s.substr(0, rb + 1)) +
                               capAtlas.MeasureWidth(gl, s.substr(rb + 1));
                    }
                }
                return capAtlas.MeasureWidth(gl, s);
            };
            std::wstring hl2Full;
            if (isHl2Style) {
                hl2Full = cap.text;
                // In HL2 mode: keep [Intercom]/[Radio] (VOX, italic), [HEV], named NPCs ([Rosenberg]/[Otis]/[G-Man]),
                // and numbered multi-NPCs ([Scientist 1]/[Scientist 2]). Strip tag only for solo generic NPCs.
                bool stripTag = (cap.cat == subs::RoleCat::NPC && cap.isGenericNpc && cap.speakerNum == 0);
                if (stripTag && !hl2Full.empty() && hl2Full[0] == L'[') {
                    size_t rb = hl2Full.find(L']');
                    if (rb != std::wstring::npos) {
                        size_t bodyStart = rb + 1;
                        while (bodyStart < hl2Full.size() && hl2Full[bodyStart] == L' ') {
                            ++bodyStart;
                        }
                        if (bodyStart < hl2Full.size()) {
                            hl2Full.erase(0, bodyStart);
                        }
                    }
                }
                for (size_t p = 0; (p = hl2Full.find(L"||", p)) != std::wstring::npos;) {
                    hl2Full.replace(p, 2, L"\n");
                }
                if (ci + 1 == state.captions.size()) {
                    hl2CapDurMs = std::max<ULONGLONG>(1, cap.expire_ms - cap.start_ms);
                    hl2CapElapsedMs = (state.gameMs >= cap.start_ms) ? (state.gameMs - cap.start_ms) : 0;
                }
            }
            const std::wstring& pageTxt = isHl2Style
                ? hl2Full
                : core::GetActiveCaptionText(cap, measureCap, state.gameMs, maxBoxW - padX * 2, cfg.max_lines_per_caption);
            std::vector<RenderLine> lines;
            auto wrapped = core::WrapTextWith(pageTxt, maxBoxW - padX * 2, measureCap);
            for (size_t wi = 0; wi < wrapped.size(); ++wi) {
                int w = (wi == 0) ? measureCap(wrapped[wi]) : capAtlas.MeasureWidth(gl, wrapped[wi]);
                lines.push_back({std::move(wrapped[wi]), cap.color, w, capAlpha, &capAtlas, wi == 0, cap.cat});
            }
            totalLines += static_cast<int>(lines.size());
            perCap.push_back(std::move(lines));
        }
        const int effMaxLines = isHl2Style ? std::min(cfg.max_lines, 4) : cfg.max_lines;
        size_t firstCap = 0;
        while (perCap.size() - firstCap > 1 && totalLines > effMaxLines) {
            totalLines -= static_cast<int>(perCap[firstCap].size());
            ++firstCap;
        }
        if (firstCap > 0) {
            state.captions.erase(state.captions.begin(), state.captions.begin() + static_cast<ptrdiff_t>(firstCap));
        }
        const int baseBottomY = screenH - (screenH * cfg.bottom_margin_pct) / 100;
        const uint8_t bgAlpha = static_cast<uint8_t>(std::clamp(cfg.bg_alpha, 0, 255));

        std::vector<RenderLine> allLines;
        uint8_t maxLineAlpha = 0;
        for (size_t ci = firstCap; ci < perCap.size(); ++ci) {
            for (auto& rl : perCap[ci]) {
                maxLineAlpha = std::max(maxLineAlpha, rl.alpha);
                allLines.push_back(std::move(rl));
            }
        }

        if (!allLines.empty() && maxLineAlpha > 0) {
            int widest = 0;
            for (const auto& rl : allLines) widest = std::max(widest, rl.w);
            int boxW = isHl2Style
                ? maxBoxW
                : std::min(widest + padX * 2, std::max(40, screenW - safeMargin * 2));
            int visibleLines = isHl2Style
                ? std::min(static_cast<int>(allLines.size()), effMaxLines)
                : static_cast<int>(allLines.size());
            int lineGap = isHl2Style ? 4 : 3;
            int boxH = visibleLines * lineStep + padY * 2 - lineGap;
            int boxX = std::clamp((screenW - boxW) / 2, safeMargin, std::max(safeMargin, screenW - safeMargin - boxW));
            int boxY = std::clamp(baseBottomY - boxH, safeMargin, std::max(safeMargin, screenH - safeMargin - boxH));

            uint8_t baseBgA = (isHl2Style && bgAlpha > 0) ? std::max<uint8_t>(bgAlpha, 155) : bgAlpha;
            uint8_t effBgA = static_cast<uint8_t>((static_cast<int>(baseBgA) * static_cast<int>(maxLineAlpha)) / 255);
            if (effBgA > 0) {
                DrawRoundedRect(gl, boxX, boxY, boxX + boxW, boxY + boxH, cfg.bg_radius,
                                static_cast<uint8_t>(cfg.bg_color.r),
                                static_cast<uint8_t>(cfg.bg_color.g),
                                static_cast<uint8_t>(cfg.bg_color.b), effBgA);
            }

            int extraLines = isHl2Style ? std::max(0, static_cast<int>(allLines.size()) - visibleLines) : 0;
            int maxScrollPx = extraLines * lineStep;
            int scrollPx = 0;
            if (extraLines > 0) {
                double stepMs = static_cast<double>(hl2CapDurMs) / static_cast<double>(extraLines + 1);
                double pos = static_cast<double>(hl2CapElapsedMs) / std::max(1.0, stepMs);
                int curStep = std::min(extraLines, static_cast<int>(std::floor(pos)));
                double stepStartMs = static_cast<double>(curStep) * stepMs;
                double animFrac = (curStep > 0)
                    ? std::clamp((static_cast<double>(hl2CapElapsedMs) - stepStartMs) / 350.0, 0.0, 1.0)
                    : 1.0;
                double ease = 1.0 - (1.0 - animFrac) * (1.0 - animFrac);
                double lineOffset = (curStep > 0) ? (static_cast<double>(curStep - 1) + ease) : 0.0;
                scrollPx = std::clamp(static_cast<int>(std::lround(lineOffset * static_cast<double>(lineStep))), 0, maxScrollPx);
                if (gl.glScissor) {
                    int scX = boxX + padX - 2;
                    int scW = std::max(1, boxW - padX * 2 + 4);
                    int scH = std::max(1, boxH - padY * 2 + 4);
                    int scY = screenH - (boxY + padY - 2 + scH);
                    gl.glScissor(scX, scY, scW, scH);
                    gl.glEnable(glfont::GlApi::kScissorTest);
                }
            }

            int y = boxY + padY - scrollPx;
            for (const auto& rl : allLines) {
                if (rl.alpha > 0 && y + lineStep > boxY + padY - 2 && y < boxY + boxH - padY + 2) {
                    int x = boxX + padX;
                    drawLineText(rl, x, y);
                }
                y += lineStep;
            }
            if (maxScrollPx > 0 && gl.glScissor) {
                gl.glDisable(glfont::GlApi::kScissorTest);
            }
        }
    }

    gl.glMatrixMode(glfont::GlApi::kTexture);
    gl.glPopMatrix();
    gl.glMatrixMode(glfont::GlApi::kModelview);
    gl.glPopMatrix();
    gl.glMatrixMode(glfont::GlApi::kProjection);
    gl.glPopMatrix();
    gl.glPopClientAttrib();
    gl.glPopAttrib();

    ReleaseSRWLockExclusive(&state.lock);
}

} // namespace render
