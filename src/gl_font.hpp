#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace glfont {

struct Glyph {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    int xOff = 0;
    int yOff = 0;
    int w = 0;
    int h = 0;
    int advance = 0;
};

// Minimal OpenGL 1.x/2.x function table resolved dynamically from opengl32.dll.
struct GlApi {
    using GLenum = unsigned int;
    using GLboolean = unsigned char;
    using GLbitfield = unsigned int;
    using GLint = int;
    using GLsizei = int;
    using GLuint = unsigned int;
    using GLfloat = float;
    using GLdouble = double;
    using GLubyte = unsigned char;

    static constexpr GLenum kTexture2D = 0x0DE1;
    static constexpr GLenum kRgba = 0x1908;
    static constexpr GLenum kUnsignedByte = 0x1401;
    static constexpr GLenum kLinear = 0x2601;
    static constexpr GLenum kClamp = 0x2900;
    static constexpr GLenum kTexMinFilter = 0x2801;
    static constexpr GLenum kTexMagFilter = 0x2800;
    static constexpr GLenum kTexWrapS = 0x2802;
    static constexpr GLenum kTexWrapT = 0x2803;
    static constexpr GLenum kBlend = 0x0BE2;
    static constexpr GLenum kSrcAlpha = 0x0302;
    static constexpr GLenum kOneMinusSrcAlpha = 0x0303;
    static constexpr GLenum kDepthTest = 0x0B71;
    static constexpr GLenum kCullFace = 0x0B44;
    static constexpr GLenum kScissorTest = 0x0C11;
    static constexpr GLenum kAlphaTest = 0x0BC0;
    static constexpr GLenum kLighting = 0x0B50;
    static constexpr GLenum kFog = 0x0B60;
    static constexpr GLenum kProjection = 0x1701;
    static constexpr GLenum kModelview = 0x1700;
    static constexpr GLenum kTexture = 0x1702;
    static constexpr GLenum kLineLoop = 0x0002;
    static constexpr GLenum kTriangleFan = 0x0006;
    static constexpr GLenum kQuads = 0x0007;
    static constexpr GLenum kTextureEnv = 0x2300;
    static constexpr GLenum kTextureEnvMode = 0x2200;
    static constexpr GLenum kModulate = 0x2100;
    static constexpr GLenum kUnpackAlignment = 0x0CF5;
    static constexpr GLenum kUnpackRowLength = 0x0CF2;
    static constexpr GLenum kUnpackSkipRows = 0x0CF3;
    static constexpr GLenum kUnpackSkipPixels = 0x0CF4;
    static constexpr GLenum kTexture0 = 0x84C0;
    static constexpr GLbitfield kAllAttribBits = 0x000FFFFF;
    static constexpr GLbitfield kClientAllAttribBits = 0xFFFFFFFF;

    HGLRC(WINAPI* wglGetCurrentContext)() = nullptr;
    PROC(WINAPI* wglGetProcAddress)(LPCSTR) = nullptr;

    void(APIENTRY* glGenTextures)(GLsizei, GLuint*) = nullptr;
    void(APIENTRY* glDeleteTextures)(GLsizei, const GLuint*) = nullptr;
    void(APIENTRY* glBindTexture)(GLenum, GLuint) = nullptr;
    void(APIENTRY* glTexParameteri)(GLenum, GLenum, GLint) = nullptr;
    void(APIENTRY* glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
    void(APIENTRY* glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) = nullptr;
    void(APIENTRY* glTexEnvi)(GLenum, GLenum, GLint) = nullptr;
    void(APIENTRY* glPixelStorei)(GLenum, GLint) = nullptr;

    void(APIENTRY* glPushAttrib)(GLbitfield) = nullptr;
    void(APIENTRY* glPopAttrib)() = nullptr;
    void(APIENTRY* glPushClientAttrib)(GLbitfield) = nullptr;
    void(APIENTRY* glPopClientAttrib)() = nullptr;
    void(APIENTRY* glEnable)(GLenum) = nullptr;
    void(APIENTRY* glDisable)(GLenum) = nullptr;
    void(APIENTRY* glScissor)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void(APIENTRY* glBlendFunc)(GLenum, GLenum) = nullptr;
    void(APIENTRY* glDepthMask)(GLboolean) = nullptr;
    void(APIENTRY* glMatrixMode)(GLenum) = nullptr;
    void(APIENTRY* glPushMatrix)() = nullptr;
    void(APIENTRY* glPopMatrix)() = nullptr;
    void(APIENTRY* glLoadIdentity)() = nullptr;
    void(APIENTRY* glOrtho)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble) = nullptr;
    void(APIENTRY* glScalef)(GLfloat, GLfloat, GLfloat) = nullptr;
    void(APIENTRY* glBegin)(GLenum) = nullptr;
    void(APIENTRY* glEnd)() = nullptr;
    void(APIENTRY* glColor4ub)(GLubyte, GLubyte, GLubyte, GLubyte) = nullptr;
    void(APIENTRY* glTexCoord2f)(GLfloat, GLfloat) = nullptr;
    void(APIENTRY* glVertex2f)(GLfloat, GLfloat) = nullptr;
    void(APIENTRY* glActiveTextureARB)(GLenum) = nullptr;

    bool loaded = false;

    std::expected<void, std::string> Load() {
        if (loaded) return {};
        HMODULE gl = GetModuleHandleA("opengl32.dll");
        if (!gl) return std::unexpected("opengl32.dll is not loaded");
        auto gp = [gl](const char* name) { return (void*)GetProcAddress(gl, name); };

        wglGetCurrentContext = reinterpret_cast<decltype(wglGetCurrentContext)>(gp("wglGetCurrentContext"));
        wglGetProcAddress = reinterpret_cast<decltype(wglGetProcAddress)>(gp("wglGetProcAddress"));
        glGenTextures = reinterpret_cast<decltype(glGenTextures)>(gp("glGenTextures"));
        glDeleteTextures = reinterpret_cast<decltype(glDeleteTextures)>(gp("glDeleteTextures"));
        glBindTexture = reinterpret_cast<decltype(glBindTexture)>(gp("glBindTexture"));
        glTexParameteri = reinterpret_cast<decltype(glTexParameteri)>(gp("glTexParameteri"));
        glTexImage2D = reinterpret_cast<decltype(glTexImage2D)>(gp("glTexImage2D"));
        glTexSubImage2D = reinterpret_cast<decltype(glTexSubImage2D)>(gp("glTexSubImage2D"));
        glTexEnvi = reinterpret_cast<decltype(glTexEnvi)>(gp("glTexEnvi"));
        glPixelStorei = reinterpret_cast<decltype(glPixelStorei)>(gp("glPixelStorei"));
        glPushAttrib = reinterpret_cast<decltype(glPushAttrib)>(gp("glPushAttrib"));
        glPopAttrib = reinterpret_cast<decltype(glPopAttrib)>(gp("glPopAttrib"));
        glPushClientAttrib = reinterpret_cast<decltype(glPushClientAttrib)>(gp("glPushClientAttrib"));
        glPopClientAttrib = reinterpret_cast<decltype(glPopClientAttrib)>(gp("glPopClientAttrib"));
        glEnable = reinterpret_cast<decltype(glEnable)>(gp("glEnable"));
        glDisable = reinterpret_cast<decltype(glDisable)>(gp("glDisable"));
        glScissor = reinterpret_cast<decltype(glScissor)>(gp("glScissor"));
        glBlendFunc = reinterpret_cast<decltype(glBlendFunc)>(gp("glBlendFunc"));
        glDepthMask = reinterpret_cast<decltype(glDepthMask)>(gp("glDepthMask"));
        glMatrixMode = reinterpret_cast<decltype(glMatrixMode)>(gp("glMatrixMode"));
        glPushMatrix = reinterpret_cast<decltype(glPushMatrix)>(gp("glPushMatrix"));
        glPopMatrix = reinterpret_cast<decltype(glPopMatrix)>(gp("glPopMatrix"));
        glLoadIdentity = reinterpret_cast<decltype(glLoadIdentity)>(gp("glLoadIdentity"));
        glOrtho = reinterpret_cast<decltype(glOrtho)>(gp("glOrtho"));
        glScalef = reinterpret_cast<decltype(glScalef)>(gp("glScalef"));
        glBegin = reinterpret_cast<decltype(glBegin)>(gp("glBegin"));
        glEnd = reinterpret_cast<decltype(glEnd)>(gp("glEnd"));
        glColor4ub = reinterpret_cast<decltype(glColor4ub)>(gp("glColor4ub"));
        glTexCoord2f = reinterpret_cast<decltype(glTexCoord2f)>(gp("glTexCoord2f"));
        glVertex2f = reinterpret_cast<decltype(glVertex2f)>(gp("glVertex2f"));

        loaded = (wglGetCurrentContext && glGenTextures && glBindTexture && glTexImage2D &&
                  glTexSubImage2D && glPushAttrib && glPopAttrib && glPushClientAttrib &&
                  glPopClientAttrib && glOrtho && glBegin && glEnd && glColor4ub);
        if (!loaded) return std::unexpected("Missing required OpenGL 1.x/2.x exports");
        return {};
    }

    void ResolveExtensions() {
        if (!glActiveTextureARB && wglGetProcAddress) {
            glActiveTextureARB = reinterpret_cast<decltype(glActiveTextureARB)>(
                (void*)wglGetProcAddress("glActiveTextureARB"));
            if (!glActiveTextureARB) {
                glActiveTextureARB = reinterpret_cast<decltype(glActiveTextureARB)>(
                    (void*)wglGetProcAddress("glActiveTexture"));
            }
        }
    }
};

// Self-contained GDI-rasterized OpenGL font atlas, independent of VGUI2.
class FontAtlas {
public:
    static constexpr int kAtlasSize = 1024;
    static constexpr int kGlyphPad = 6;
    static constexpr int kTopPad = 2;

    ~FontAtlas() { DestroyGdi(); }

    std::expected<void, std::string> EnsureReady(GlApi& gl, HGLRC ctx, const std::wstring& faceName, int tall,
                                                 bool italic = false, bool bold = true) {
        if (auto res = gl.Load(); !res) return std::unexpected(res.error());
        if (!ctx) return std::unexpected("No active OpenGL HGLRC context");
        gl.ResolveExtensions();

        if (tex_ != 0 && ctx_ == ctx && face_ == faceName && tall_ == tall &&
            italic_ == italic && bold_ == bold) {
            return {};
        }
        if (tex_ != 0 && ctx_ == ctx && gl.glDeleteTextures) {
            gl.glDeleteTextures(1, &tex_);
        }
        tex_ = 0;
        ctx_ = ctx;
        face_ = faceName;
        tall_ = tall;
        italic_ = italic;
        bold_ = bold;
        glyphs_.clear();
        penX_ = 1;
        penY_ = 1;
        rowH_ = 0;

        if (auto gdiRes = InitGdi(faceName, tall, italic, bold); !gdiRes) {
            return std::unexpected(gdiRes.error());
        }

        std::vector<uint8_t> atlasRgba((size_t)kAtlasSize * kAtlasSize * 4, 0);
        std::span<uint8_t> atlasSpan(atlasRgba);
        auto bake = [&](wchar_t ch) { RasterizeIntoBuffer(ch, atlasSpan); };

        for (wchar_t ch = 0x0020; ch <= 0x007E; ++ch) bake(ch);
        bake(0x0401);
        for (wchar_t ch = 0x0410; ch <= 0x044F; ++ch) bake(ch);
        bake(0x0451);
        for (wchar_t ch : {L'\u2013', L'\u2014', L'\u2018', L'\u2019', L'\u201C', L'\u201D',
                           L'\u2026', L'\u00AB', L'\u00BB', L'\u2116', L'\u00A0'}) {
            bake(ch);
        }

        gl.glPushClientAttrib(GlApi::kClientAllAttribBits);
        gl.glPixelStorei(GlApi::kUnpackAlignment, 1);
        gl.glPixelStorei(GlApi::kUnpackRowLength, 0);
        gl.glPixelStorei(GlApi::kUnpackSkipRows, 0);
        gl.glPixelStorei(GlApi::kUnpackSkipPixels, 0);

        gl.glGenTextures(1, &tex_);
        gl.glBindTexture(GlApi::kTexture2D, tex_);
        gl.glTexParameteri(GlApi::kTexture2D, GlApi::kTexMinFilter, GlApi::kLinear);
        gl.glTexParameteri(GlApi::kTexture2D, GlApi::kTexMagFilter, GlApi::kLinear);
        gl.glTexParameteri(GlApi::kTexture2D, GlApi::kTexWrapS, GlApi::kClamp);
        gl.glTexParameteri(GlApi::kTexture2D, GlApi::kTexWrapT, GlApi::kClamp);
        gl.glTexImage2D(GlApi::kTexture2D, 0, GlApi::kRgba, kAtlasSize, kAtlasSize, 0,
                        GlApi::kRgba, GlApi::kUnsignedByte, atlasRgba.data());
        gl.glPopClientAttrib();
        if (tex_ == 0) return std::unexpected("glGenTextures returned 0");
        return {};
    }

    int LineHeight() const { return lineH_; }
    unsigned int Texture() const { return tex_; }

    const Glyph* GetGlyph(GlApi& gl, wchar_t ch) {
        auto it = glyphs_.find(ch);
        if (it != glyphs_.end()) return &it->second;
        return RasterizeAndUpload(gl, ch);
    }

    int MeasureWidth(GlApi& gl, const std::wstring& s) {
        int w = 0;
        for (wchar_t ch : s) {
            if (ch == L'\r' || ch == L'\n') continue;
            const Glyph* g = GetGlyph(gl, ch);
            if (g) w += g->advance;
        }
        return w;
    }

    void DrawText(GlApi& gl, int x, int y, const std::wstring& s, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                  bool shadow = true, bool glow = false,
                  uint8_t gr = 255, uint8_t gg = 195, uint8_t gb = 55, uint8_t ga = 55) {
        if (s.empty() || !tex_) return;
        gl.glBindTexture(GlApi::kTexture2D, tex_);
        gl.glBegin(GlApi::kQuads);

        if (glow && ga > 0) {
            // Outer halo (±2px) + inner halo (±1px) for visible CRT phosphor glow.
            uint8_t outerA = (uint8_t)(((int)ga * (int)a) / 510);
            uint8_t innerA = (uint8_t)(((int)ga * (int)a) / 255);
            if (outerA > 0) {
                gl.glColor4ub(gr, gg, gb, outerA);
                static constexpr int kDx2[8] = {-2, 0, 2, -2, 2, -2, 0, 2};
                static constexpr int kDy2[8] = {-2, -2, -2, 0, 0, 2, 2, 2};
                for (int k = 0; k < 8; ++k) {
                    EmitStringQuads(gl, (float)(x + kDx2[k]), (float)(y + kDy2[k]), s);
                }
            }
            gl.glColor4ub(gr, gg, gb, innerA);
            static constexpr int kDx1[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
            static constexpr int kDy1[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
            for (int k = 0; k < 8; ++k) {
                EmitStringQuads(gl, (float)(x + kDx1[k]), (float)(y + kDy1[k]), s);
            }
        } else if (shadow) {
            // 1px black drop shadow for legibility over bright scenes (matches VGUI2 0x10).
            gl.glColor4ub(0, 0, 0, a);
            EmitStringQuads(gl, (float)(x + 1), (float)(y + 1), s);
        }

        gl.glColor4ub(r, g, b, a);
        EmitStringQuads(gl, (float)x, (float)y, s);

        gl.glEnd();
    }

    void Invalidate() {
        tex_ = 0;
        ctx_ = nullptr;
    }

private:
    std::expected<void, std::string> InitGdi(const std::wstring& faceName, int tall,
                                             bool italic = false, bool bold = true) {
        DestroyGdi();
        hdc_ = CreateCompatibleDC(nullptr);
        if (!hdc_) return std::unexpected("CreateCompatibleDC failed");

        int weight = bold ? FW_BOLD : FW_NORMAL;
        hfont_ = CreateFontW(-tall, 0, 0, 0, weight, italic ? TRUE : FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, faceName.c_str());
        if (!hfont_) {
            hfont_ = CreateFontW(-tall, 0, 0, 0, weight, italic ? TRUE : FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Tahoma");
        }
        if (!hfont_) {
            DestroyGdi();
            return std::unexpected("CreateFontW failed");
        }

        oldFont_ = (HFONT)SelectObject(hdc_, hfont_);
        TEXTMETRICW tm{};
        GetTextMetricsW(hdc_, &tm);
        lineH_ = std::max(14, (int)tm.tmHeight);
        scratchW_ = std::max(128, lineH_ * 3);
        scratchH_ = lineH_ + kTopPad + 6;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = scratchW_;
        bmi.bmiHeader.biHeight = -scratchH_;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        dib_ = CreateDIBSection(hdc_, &bmi, DIB_RGB_COLORS, &dibBits_, nullptr, 0);
        if (!dib_ || !dibBits_) {
            DestroyGdi();
            return std::unexpected("CreateDIBSection failed");
        }
        oldBmp_ = (HBITMAP)SelectObject(hdc_, dib_);

        SetBkMode(hdc_, TRANSPARENT);
        SetTextColor(hdc_, RGB(255, 255, 255));
        SetTextAlign(hdc_, TA_LEFT | TA_TOP);
        return {};
    }

    void DestroyGdi() {
        if (hdc_) {
            if (oldBmp_) SelectObject(hdc_, oldBmp_);
            if (oldFont_) SelectObject(hdc_, oldFont_);
        }
        if (dib_) DeleteObject(dib_);
        if (hfont_) DeleteObject(hfont_);
        if (hdc_) DeleteDC(hdc_);
        hdc_ = nullptr;
        hfont_ = nullptr;
        oldFont_ = nullptr;
        dib_ = nullptr;
        oldBmp_ = nullptr;
        dibBits_ = nullptr;
    }

    bool RasterizeGlyphPixels(wchar_t ch, int& outAdv, int& outW, int& outH, std::vector<uint8_t>& outRgba) {
        if (!hdc_ || !dibBits_) return false;
        SIZE sz{};
        GetTextExtentPoint32W(hdc_, &ch, 1, &sz);
        outAdv = std::max(0, (int)sz.cx);
        outW = std::clamp(outAdv + kGlyphPad * 2, 1, scratchW_);
        outH = std::min(lineH_ + kTopPad + 2, scratchH_);

        std::memset(dibBits_, 0, (size_t)scratchW_ * scratchH_ * 4);
        ExtTextOutW(hdc_, kGlyphPad, kTopPad, 0, nullptr, &ch, 1, nullptr);
        GdiFlush();

        outRgba.resize((size_t)outW * outH * 4);
        const uint8_t* src = static_cast<const uint8_t*>(dibBits_);
        for (int row = 0; row < outH; ++row) {
            const uint8_t* srow = src + (size_t)row * scratchW_ * 4;
            uint8_t* drow = outRgba.data() + (size_t)row * outW * 4;
            for (int col = 0; col < outW; ++col) {
                uint8_t a = std::max({srow[col * 4 + 0], srow[col * 4 + 1], srow[col * 4 + 2]});
                drow[col * 4 + 0] = 255;
                drow[col * 4 + 1] = 255;
                drow[col * 4 + 2] = 255;
                drow[col * 4 + 3] = a;
            }
        }
        return true;
    }

    bool AllocAtlasSlot(int w, int h, int& outX, int& outY) {
        if (penX_ + w + 1 > kAtlasSize) {
            penX_ = 1;
            penY_ += rowH_ + 1;
            rowH_ = 0;
        }
        if (penY_ + h + 1 > kAtlasSize) return false;
        outX = penX_;
        outY = penY_;
        penX_ += w + 1;
        rowH_ = std::max(rowH_, h);
        return true;
    }

    void StoreGlyph(wchar_t ch, int ax, int ay, int adv, int w, int h) {
        Glyph g;
        g.u0 = (float)ax / (float)kAtlasSize;
        g.v0 = (float)ay / (float)kAtlasSize;
        g.u1 = (float)(ax + w) / (float)kAtlasSize;
        g.v1 = (float)(ay + h) / (float)kAtlasSize;
        g.xOff = -kGlyphPad;
        g.yOff = -kTopPad;
        g.w = w;
        g.h = h;
        g.advance = adv;
        glyphs_[ch] = g;
    }

    void RasterizeIntoBuffer(wchar_t ch, std::span<uint8_t> atlasRgba) {
        if (glyphs_.count(ch)) return;
        int adv = 0, w = 0, h = 0;
        std::vector<uint8_t> tmp;
        if (!RasterizeGlyphPixels(ch, adv, w, h, tmp)) return;
        int ax = 0, ay = 0;
        if (!AllocAtlasSlot(w, h, ax, ay)) return;
        std::span<const uint8_t> src(tmp);
        for (int row = 0; row < h; ++row) {
            auto srcRow = src.subspan((size_t)row * w * 4, (size_t)w * 4);
            auto dstRow = atlasRgba.subspan(((size_t)(ay + row) * kAtlasSize + ax) * 4, (size_t)w * 4);
            std::ranges::copy(srcRow, dstRow.begin());
        }
        StoreGlyph(ch, ax, ay, adv, w, h);
    }

    const Glyph* RasterizeAndUpload(GlApi& gl, wchar_t ch) {
        int adv = 0, w = 0, h = 0;
        std::vector<uint8_t> tmp;
        if (!RasterizeGlyphPixels(ch, adv, w, h, tmp)) return nullptr;
        int ax = 0, ay = 0;
        if (!AllocAtlasSlot(w, h, ax, ay)) return nullptr;

        if (tex_ && gl.glTexSubImage2D) {
            gl.glPushClientAttrib(GlApi::kClientAllAttribBits);
            gl.glPixelStorei(GlApi::kUnpackAlignment, 1);
            gl.glPixelStorei(GlApi::kUnpackRowLength, 0);
            gl.glPixelStorei(GlApi::kUnpackSkipRows, 0);
            gl.glPixelStorei(GlApi::kUnpackSkipPixels, 0);
            gl.glBindTexture(GlApi::kTexture2D, tex_);
            gl.glTexSubImage2D(GlApi::kTexture2D, 0, ax, ay, w, h,
                               GlApi::kRgba, GlApi::kUnsignedByte, tmp.data());
            gl.glPopClientAttrib();
        }
        StoreGlyph(ch, ax, ay, adv, w, h);
        return &glyphs_[ch];
    }

    void EmitStringQuads(GlApi& gl, float startX, float startY, const std::wstring& s) {
        float cx = startX;
        for (wchar_t ch : s) {
            if (ch == L'\r' || ch == L'\n') continue;
            auto it = glyphs_.find(ch);
            if (it == glyphs_.end()) continue;
            const Glyph& g = it->second;
            float x0 = cx + (float)g.xOff;
            float y0 = startY + (float)g.yOff;
            float x1 = x0 + (float)g.w;
            float y1 = y0 + (float)g.h;
            gl.glTexCoord2f(g.u0, g.v0); gl.glVertex2f(x0, y0);
            gl.glTexCoord2f(g.u1, g.v0); gl.glVertex2f(x1, y0);
            gl.glTexCoord2f(g.u1, g.v1); gl.glVertex2f(x1, y1);
            gl.glTexCoord2f(g.u0, g.v1); gl.glVertex2f(x0, y1);
            cx += (float)g.advance;
        }
    }

    HDC hdc_ = nullptr;
    HFONT hfont_ = nullptr;
    HFONT oldFont_ = nullptr;
    HBITMAP dib_ = nullptr;
    HBITMAP oldBmp_ = nullptr;
    void* dibBits_ = nullptr;
    int scratchW_ = 0;
    int scratchH_ = 0;

    HGLRC ctx_ = nullptr;
    std::wstring face_;
    int tall_ = 0;
    bool italic_ = false;
    bool bold_ = true;
    int lineH_ = 16;
    unsigned int tex_ = 0;
    int penX_ = 1;
    int penY_ = 1;
    int rowH_ = 0;
    std::unordered_map<wchar_t, Glyph> glyphs_;
};

} // namespace glfont
