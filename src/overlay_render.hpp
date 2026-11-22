#pragma once

/**
 * @file overlay_render.hpp
 * @brief OpenGL 1.x overlay renderer for subtitles and localized chapter titles.
 *
 * Supports two visual modes configured via [Visual] style= in subtitles.ini:
 *  - "default": dynamic-width rounded plate, colored bold [Speaker] + white body,
 *               italic VOX/SFX, and paginated long lines.
 *  - "hl2":     fixed-width 4-line plate (48% screen width), left-aligned role-colored
 *               text, and stepped 350ms vertical scroll clipped via glScissor.
 *
 * Plate geometry uses integer-pixel horizontal GL_QUADS strips (DrawRoundedRect)
 * and safe-area screen margins to stay crisp at any resolution.
 */

#include "app_state.hpp"
#include "text_layout.hpp"
#include "flashlight_gl.hpp"
#include "hud_fbo_scaler.hpp"

gl_flash::GlFlashlight g_glFlashlight;
hud_fbo::HudFboScaler g_hudFbo;
bool g_hudFboCapturedThisFrame = false;

void DrawActiveTitles(int screenW, int screenH, int fontH) {
    ULONGLONG nowMs = GetTickCount64();
    int lineStep = fontH + 4;
    for (size_t idx = 0; idx < g_activeTitles.size(); ) {
        const auto& at = g_activeTitles[idx];
        ULONGLONG elapsed = (nowMs >= at.start_ms) ? (nowMs - at.start_ms) : 0;
        ULONGLONG total = at.fadein_ms + at.hold_ms + at.fadeout_ms;
        if (elapsed >= total) {
            g_activeTitles.erase(g_activeTitles.begin() + idx);
            continue;
        }
        idx++;

        float alphaF = 1.0f;
        if (at.fadein_ms > 0 && elapsed < at.fadein_ms) {
            alphaF = (float)elapsed / (float)at.fadein_ms;
        } else if (at.fadeout_ms > 0 && elapsed > at.fadein_ms + at.hold_ms) {
            ULONGLONG outEl = elapsed - (at.fadein_ms + at.hold_ms);
            alphaF = 1.0f - (float)outEl / (float)at.fadeout_ms;
        }
        int alpha = std::clamp((int)(alphaF * 180.0f), 0, 180);
        if (alpha <= 0) continue;

        std::vector<std::wstring> lines;
        for (auto part : std::wstring_view(at.text) | std::views::split(L'\n')) {
            lines.emplace_back(part.begin(), part.end());
        }

        int totalH = (int)lines.size() * lineStep;
        int baseY = (at.y < 0.0f) ? (screenH - totalH) / 2 : (int)(at.y * (float)screenH);

        for (size_t li = 0; li < lines.size(); ++li) {
            if (lines[li].empty()) continue;
            int w = g_titleAtlas.MeasureWidth(g_gl, lines[li]);
            int x = (at.x < 0.0f) ? (screenW - w) / 2 : (int)(at.x * (float)screenW);
            int y = baseY + (int)li * lineStep;
            g_titleAtlas.DrawText(g_gl, x, y, lines[li], 255, 255, 255, (uint8_t)alpha, false);
        }
    }
}

void DrawRoundedRect(int x0, int y0, int x1, int y1, int radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x1 <= x0 || y1 <= y0 || a == 0) return;
    int rad = std::max(0, std::min({radius, (x1 - x0) / 2, (y1 - y0) / 2}));
    g_gl.glDisable(glfont::GlApi::kTexture2D);
    g_gl.glColor4ub(r, g, b, a);
    g_gl.glBegin(glfont::GlApi::kQuads);
    auto emitQuad = [](int qx0, int qy0, int qx1, int qy1) {
        if (qx1 <= qx0 || qy1 <= qy0) return;
        g_gl.glVertex2f((float)qx0, (float)qy0);
        g_gl.glVertex2f((float)qx1, (float)qy0);
        g_gl.glVertex2f((float)qx1, (float)qy1);
        g_gl.glVertex2f((float)qx0, (float)qy1);
    };
    if (rad <= 0) {
        emitQuad(x0, y0, x1, y1);
    } else {
        // Pixel-snapped 1px horizontal strips for crisp, symmetric corners without subpixel seams
        for (int row = 0; row < rad; ++row) {
            double dy = (double)rad - (double)row - 0.5;
            double dx = std::sqrt(std::max(0.0, (double)(rad * rad) - dy * dy));
            int inset = std::max(0, rad - (int)std::lround(dx));
            emitQuad(x0 + inset, y0 + row, x1 - inset, y0 + row + 1);
            emitQuad(x0 + inset, y1 - row - 1, x1 - inset, y1 - row);
        }
        emitQuad(x0, y0 + rad, x1, y1 - rad);
    }
    g_gl.glEnd();
    g_gl.glEnable(glfont::GlApi::kTexture2D);
}

bool IsGamePausedLocked(ULONGLONG nowRealMs) {
    if (g_lastHudRedrawRealMs == 0 || (nowRealMs - g_lastHudRedrawRealMs) > 180) return true;
    if (g_lastTimeAdvanceRealMs == 0 || (nowRealMs - g_lastTimeAdvanceRealMs) > 180) return true;
    return false;
}

void RenderSubtitlesOverlay() {
    if (g_screenW <= 0 || g_screenH <= 0) return;
    if (!g_gl.Load()) return;
    HGLRC ctx = g_gl.wglGetCurrentContext();
    if (!ctx) return;

    AcquireSRWLockExclusive(&g_lock);
    if (IsGamePausedLocked(GetTickCount64()) || (g_captions.empty() && g_activeTitles.empty())) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }

    int tall = (g_cfg.font_size > 0) ? g_cfg.font_size : std::max(14, (g_screenH * 20) / 1080);
    std::wstring face = Utf8ToWide(g_cfg.font_name);
    if (face.empty()) face = L"Tahoma";
    if (auto res = g_atlas.EnsureReady(g_gl, ctx, face, tall, false, false); !res) {
        LogMsg("[gl] g_atlas failed: %s", res.error().c_str());
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    bool voxReady = g_voxAtlas.EnsureReady(g_gl, ctx, L"Tahoma", tall, true, false).has_value();
    bool speakerReady = g_speakerAtlas.EnsureReady(g_gl, ctx, face, tall, false, true).has_value();

    g_gl.glPushAttrib(glfont::GlApi::kAllAttribBits);
    g_gl.glPushClientAttrib(glfont::GlApi::kClientAllAttribBits);

    if (g_gl.glActiveTextureARB) {
        for (int u = 1; u <= 3; ++u) {
            g_gl.glActiveTextureARB(glfont::GlApi::kTexture0 + u);
            g_gl.glDisable(glfont::GlApi::kTexture2D);
        }
        g_gl.glActiveTextureARB(glfont::GlApi::kTexture0);
    }

    g_gl.glDisable(glfont::GlApi::kDepthTest);
    g_gl.glDisable(glfont::GlApi::kCullFace);
    g_gl.glDisable(glfont::GlApi::kScissorTest);
    g_gl.glDisable(glfont::GlApi::kAlphaTest);
    g_gl.glDisable(glfont::GlApi::kLighting);
    g_gl.glDisable(glfont::GlApi::kFog);
    g_gl.glDepthMask(0);
    g_gl.glEnable(glfont::GlApi::kBlend);
    g_gl.glBlendFunc(glfont::GlApi::kSrcAlpha, glfont::GlApi::kOneMinusSrcAlpha);
    g_gl.glEnable(glfont::GlApi::kTexture2D);
    g_gl.glTexEnvi(glfont::GlApi::kTextureEnv, glfont::GlApi::kTextureEnvMode, glfont::GlApi::kModulate);

    g_gl.glMatrixMode(glfont::GlApi::kProjection);
    g_gl.glPushMatrix();
    g_gl.glLoadIdentity();
    g_gl.glOrtho(0.0, (double)g_screenW, (double)g_screenH, 0.0, -1.0, 1.0);
    g_gl.glMatrixMode(glfont::GlApi::kModelview);
    g_gl.glPushMatrix();
    g_gl.glLoadIdentity();
    g_gl.glMatrixMode(glfont::GlApi::kTexture);
    g_gl.glPushMatrix();
    g_gl.glLoadIdentity();

    int fontH = g_atlas.LineHeight();

    if (!g_activeTitles.empty()) {
        int titleTall = std::max(16, (g_screenH * 24) / 1080);
        if (g_titleAtlas.EnsureReady(g_gl, ctx, L"Trebuchet MS", titleTall, false, false).has_value()) {
            DrawActiveTitles(g_screenW, g_screenH, g_titleAtlas.LineHeight());
        }
    }

    if (!g_captions.empty()) {
        const bool isHl2Style = (g_cfg.style == "hl2");
        const int padX = 16, padY = isHl2Style ? 10 : 8;
        const int safeMargin = std::max(16, std::min(g_screenW, g_screenH) / 40);
        const int widthPct = (isHl2Style && g_cfg.max_width_pct == 62) ? 48 : g_cfg.max_width_pct;
        int maxBoxW = std::min((g_screenW * widthPct) / 100,
                               std::max(40, g_screenW - safeMargin * 2));
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
            const uint8_t br = isHl2Style ? rl.col.r : 255;
            const uint8_t bg = isHl2Style ? rl.col.g : 255;
            const uint8_t bb = isHl2Style ? rl.col.b : 255;
            if (IsSfxCat(rl.cat)) {
                a.DrawText(g_gl, x, y, rl.txt, rl.col.r, rl.col.g, rl.col.b, rl.alpha);
                return;
            }
            if (rl.isFirstLine && !rl.txt.empty() && rl.txt[0] == L'[') {
                size_t rb = rl.txt.find(L']');
                if (rb != std::wstring::npos) {
                    std::wstring speaker = rl.txt.substr(0, rb + 1);
                    std::wstring body = rl.txt.substr(rb + 1);
                    glfont::FontAtlas& spAtlas = speakerReady ? g_speakerAtlas : a;
                    spAtlas.DrawText(g_gl, x, y, speaker, rl.col.r, rl.col.g, rl.col.b, rl.alpha);
                    if (!body.empty()) {
                        int spW = spAtlas.MeasureWidth(g_gl, speaker);
                        a.DrawText(g_gl, x + spW, y, body, br, bg, bb, rl.alpha);
                    }
                    return;
                }
            }
            a.DrawText(g_gl, x, y, rl.txt, br, bg, bb, rl.alpha);
        };

        std::vector<std::vector<RenderLine>> perCap;
        perCap.reserve(g_captions.size());
        int totalLines = 0;
        ULONGLONG hl2CapElapsedMs = 0;
        ULONGLONG hl2CapDurMs = 1;
        for (size_t ci = 0; ci < g_captions.size(); ++ci) {
            auto& cap = g_captions[ci];
            float fIn = (g_gameMs >= cap.start_ms)
                ? std::clamp((float)(g_gameMs - cap.start_ms) / 150.0f, 0.0f, 1.0f)
                : 0.0f;
            float fOut = (cap.expire_ms > g_gameMs)
                ? std::clamp((float)(cap.expire_ms - g_gameMs) / 220.0f, 0.0f, 1.0f)
                : 0.0f;
            uint8_t capAlpha = (uint8_t)std::lround(255.0f * std::min(fIn, fOut));

            glfont::FontAtlas& capAtlas = (voxReady && (cap.cat == subs::RoleCat::VOX || IsSfxCat(cap.cat)))
                ? g_voxAtlas : g_atlas;
            auto measureCap = [&](const std::wstring& s) -> int {
                if (!IsSfxCat(cap.cat) && !s.empty() && s[0] == L'[') {
                    size_t rb = s.find(L']');
                    if (rb != std::wstring::npos) {
                        glfont::FontAtlas& spAtlas = speakerReady ? g_speakerAtlas : capAtlas;
                        return spAtlas.MeasureWidth(g_gl, s.substr(0, rb + 1)) +
                               capAtlas.MeasureWidth(g_gl, s.substr(rb + 1));
                    }
                }
                return capAtlas.MeasureWidth(g_gl, s);
            };
            std::wstring hl2Full;
            if (isHl2Style) {
                hl2Full = cap.text;
                for (size_t p = 0; (p = hl2Full.find(L"||", p)) != std::wstring::npos;) {
                    hl2Full.replace(p, 2, L"\n");
                }
                if (ci + 1 == g_captions.size()) {
                    hl2CapDurMs = std::max<ULONGLONG>(1, cap.expire_ms - cap.start_ms);
                    hl2CapElapsedMs = (g_gameMs >= cap.start_ms) ? (g_gameMs - cap.start_ms) : 0;
                }
            }
            const std::wstring& pageTxt = isHl2Style
                ? hl2Full
                : GetActiveCaptionText(cap, measureCap, g_gameMs, maxBoxW - padX * 2, g_cfg.max_lines_per_caption);
            std::vector<RenderLine> lines;
            auto wrapped = WrapTextWith(pageTxt, maxBoxW - padX * 2, measureCap);
            for (size_t wi = 0; wi < wrapped.size(); ++wi) {
                int w = (wi == 0) ? measureCap(wrapped[wi]) : capAtlas.MeasureWidth(g_gl, wrapped[wi]);
                lines.push_back({std::move(wrapped[wi]), cap.color, w, capAlpha, &capAtlas, wi == 0, cap.cat});
            }
            totalLines += (int)lines.size();
            perCap.push_back(std::move(lines));
        }
        const int effMaxLines = isHl2Style ? std::min(g_cfg.max_lines, 4) : g_cfg.max_lines;
        size_t firstCap = 0;
        while (perCap.size() - firstCap > 1 && totalLines > effMaxLines) {
            totalLines -= (int)perCap[firstCap].size();
            ++firstCap;
        }
        if (firstCap > 0) {
            g_captions.erase(g_captions.begin(), g_captions.begin() + firstCap);
        }
        const int baseBottomY = g_screenH - (g_screenH * g_cfg.bottom_margin_pct) / 100;
        const uint8_t bgAlpha = (uint8_t)std::clamp(g_cfg.bg_alpha, 0, 255);

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
                : std::min(widest + padX * 2, std::max(40, g_screenW - safeMargin * 2));
            int visibleLines = isHl2Style
                ? std::min((int)allLines.size(), effMaxLines)
                : (int)allLines.size();
            int lineGap = isHl2Style ? 4 : 3;
            int boxH = visibleLines * lineStep + padY * 2 - lineGap;
            int boxX = std::clamp((g_screenW - boxW) / 2, safeMargin, std::max(safeMargin, g_screenW - safeMargin - boxW));
            int boxY = std::clamp(baseBottomY - boxH, safeMargin, std::max(safeMargin, g_screenH - safeMargin - boxH));

            uint8_t baseBgA = (isHl2Style && bgAlpha > 0) ? std::max<uint8_t>(bgAlpha, 155) : bgAlpha;
            uint8_t effBgA = (uint8_t)(((int)baseBgA * (int)maxLineAlpha) / 255);
            if (effBgA > 0) {
                DrawRoundedRect(boxX, boxY, boxX + boxW, boxY + boxH, g_cfg.bg_radius,
                                g_cfg.bg_color.r, g_cfg.bg_color.g, g_cfg.bg_color.b, effBgA);
            }

            int extraLines = isHl2Style ? std::max(0, (int)allLines.size() - visibleLines) : 0;
            int maxScrollPx = extraLines * lineStep;
            int scrollPx = 0;
            if (extraLines > 0) {
                double stepMs = (double)hl2CapDurMs / (double)(extraLines + 1);
                double pos = (double)hl2CapElapsedMs / std::max(1.0, stepMs);
                int curStep = std::min(extraLines, (int)std::floor(pos));
                double stepStartMs = (double)curStep * stepMs;
                double animFrac = (curStep > 0)
                    ? std::clamp(((double)hl2CapElapsedMs - stepStartMs) / 350.0, 0.0, 1.0)
                    : 1.0;
                double ease = 1.0 - (1.0 - animFrac) * (1.0 - animFrac);
                double lineOffset = (curStep > 0) ? ((double)(curStep - 1) + ease) : 0.0;
                scrollPx = std::clamp((int)std::lround(lineOffset * (double)lineStep), 0, maxScrollPx);
                if (g_gl.glScissor) {
                    int scX = boxX + padX - 2;
                    int scW = std::max(1, boxW - padX * 2 + 4);
                    int scH = std::max(1, boxH - padY * 2 + 4);
                    int scY = g_screenH - (boxY + padY - 2 + scH);
                    g_gl.glScissor(scX, scY, scW, scH);
                    g_gl.glEnable(glfont::GlApi::kScissorTest);
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
            if (maxScrollPx > 0 && g_gl.glScissor) {
                g_gl.glDisable(glfont::GlApi::kScissorTest);
            }
        }
    }

    g_gl.glMatrixMode(glfont::GlApi::kTexture);
    g_gl.glPopMatrix();
    g_gl.glMatrixMode(glfont::GlApi::kModelview);
    g_gl.glPopMatrix();
    g_gl.glMatrixMode(glfont::GlApi::kProjection);
    g_gl.glPopMatrix();
    g_gl.glPopClientAttrib();
    g_gl.glPopAttrib();

    ReleaseSRWLockExclusive(&g_lock);
}

BOOL WINAPI Hook_WglSwapBuffers(HDC hdc) {
    if (g_hudFboCapturedThisFrame && g_screenW > 0 && g_screenH > 0 && g_gl.Load().has_value()) {
        g_hudFboCapturedThisFrame = false;
        g_hudFbo.CompositeScaled(g_gl, g_screenW, g_screenH, g_cfg.hud_scale);
    }
    RenderSubtitlesOverlay();
    return g_origWglSwapBuffers ? g_origWglSwapBuffers(hdc) : TRUE;
}

int __cdecl Hook_HUD_Redraw(float time, int intermission) {
    if (g_cfg.enable_gl_flashlight && !intermission && g_screenW > 0 && g_screenH > 0 && g_gl.Load().has_value()) {
        if (HGLRC ctx = g_gl.wglGetCurrentContext()) {
            AcquireSRWLockShared(&g_lock);
            bool active = !IsGamePausedLocked(GetTickCount64()) && IsFlashlightActiveLocked();
            float intensity = g_cfg.gl_flashlight_intensity;
            ReleaseSRWLockShared(&g_lock);
            if (active) {
                g_glFlashlight.Render(g_gl, ctx, g_screenW, g_screenH, intensity);
            }
        }
    }
    g_prevHealthMaxX = g_curHealthMaxX;
    g_prevArmorMinX = g_curArmorMinX;
    g_curHealthMaxX = 0;
    g_curArmorMinX = 100000;

    bool useFbo = !intermission && g_cfg.hud_scale > 1.01f &&
                  g_screenW >= 800 && g_screenH >= 600 && g_gl.Load().has_value();
    bool captured = false;
    if (useFbo) {
        if (HGLRC ctx = g_gl.wglGetCurrentContext()) {
            captured = g_hudFbo.BeginCapture(g_gl, ctx, g_screenW, g_screenH);
        }
    }
    int ret = g_origHudRedraw ? g_origHudRedraw(time, intermission) : 0;
    if (captured) {
        g_hudFbo.EndCapture();
        g_hudFboCapturedThisFrame = true;
    }
    CheckIniHotReload();
    if (!g_engfuncs || !g_engfuncs->pfnGetScreenInfo) return ret;

    SCREENINFO si{};
    si.iSize = sizeof(si);
    g_engfuncs->pfnGetScreenInfo(&si);
    if (si.iWidth <= 0 || si.iHeight <= 0) return ret;

    float curClientTime = (g_engfuncs->GetClientTime) ? g_engfuncs->GetClientTime() : time;
    ULONGLONG realNow = GetTickCount64();

    AcquireSRWLockExclusive(&g_lock);
    g_screenW = si.iWidth;
    g_screenH = si.iHeight;
    g_lastHudRedrawRealMs = realNow;
    bool rewound = (curClientTime > 0.0f && curClientTime + 0.01f < g_lastClientTime);
    if (g_resetRequested || rewound) {
        ClearCaptionBuffersLocked();
        g_resetRequested = false;
        g_clockPrevTime = curClientTime;   // do not advance on discontinuity
        g_lastTimeAdvanceRealMs = realNow;
    }
    g_lastClientTime = curClientTime;

    // Advance the game clock by the client-time delta (frozen while paused,
    // capped per frame so a level load / hitch doesn't expire everything).
    if (g_clockPrevTime < 0.0f) {
        g_lastTimeAdvanceRealMs = realNow;
    } else {
        float dt = curClientTime - g_clockPrevTime;
        if (dt > 0.0f) {
            g_gameMs += (ULONGLONG)(std::min(dt, 0.25f) * 1000.0f);
            g_lastTimeAdvanceRealMs = realNow;
        }
    }
    g_clockPrevTime = curClientTime;
    const ULONGLONG nowMs = g_gameMs;

    if (g_hasViewOrg) {
        for (auto& c : g_captions) {
            if (c.hasOrigin && c.fadeDist > 0.0f && c.expire_ms > nowMs + 250ULL) {
                float dx = c.origin[0] - g_viewOrg[0];
                float dy = c.origin[1] - g_viewOrg[1];
                float dz = c.origin[2] - g_viewOrg[2];
                if (dx * dx + dy * dy + dz * dz > c.fadeDist * c.fadeDist) {
                    c.expire_ms = nowMs + 250ULL;
                }
            }
        }
    }
    std::erase_if(g_captions, [nowMs](const ActiveCaption& c) { return c.expire_ms <= nowMs; });
    bool firePowerRestored = false;
    if (g_pendingHevPowerAtMs != 0 && nowMs >= g_pendingHevPowerAtMs) {
        g_pendingHevPowerAtMs = 0;
        firePowerRestored = (g_prevBattery > 0);
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (firePowerRestored) {
        TryTriggerRestoredHev(kHevPowerRestored, "!HEV_D00");
    }
    return ret;
}

void __cdecl Hook_V_CalcRefdef(ref_params_t* pparams) {
    if (g_origCalcRefdef) g_origCalcRefdef(pparams);
    if (pparams) {
        g_viewOrg[0] = pparams->vieworg[0];
        g_viewOrg[1] = pparams->vieworg[1];
        g_viewOrg[2] = pparams->vieworg[2];
        g_viewAngles[0] = pparams->viewangles[0];
        g_viewAngles[1] = pparams->viewangles[1];
        g_viewAngles[2] = pparams->viewangles[2];
        g_hasViewOrg = true;
        ULONGLONG now = GetTickCount64();
        float dt = (g_swayLastRealMs > 0 && now > g_swayLastRealMs)
            ? std::clamp((float)(now - g_swayLastRealMs) * 0.001f, 0.001f, 0.1f)
            : 0.016f;
        g_swayLastRealMs = now;
        g_glFlashlight.UpdateSway(pparams->viewangles[0], pparams->viewangles[1], dt);
    }
}

void __cdecl Hook_HUD_Reset() {
    AcquireSRWLockExclusive(&g_lock);
    ClearCaptionBuffersLocked();
    ResetHevStateLocked();
    g_glFlashlight.ResetSway();
    g_clockPrevTime = -1.0f;
    g_lastClientTime = 0.0f;
    g_hasViewOrg = false;
    ReleaseSRWLockExclusive(&g_lock);
    if (g_origHudReset) g_origHudReset();
}

