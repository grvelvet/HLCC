#include "render/gl_font.hpp"

#include <algorithm>
#include <cstring>
#include <ranges>

namespace glfont {

std::expected<void, std::string> GlApi::Load() {
    if (loaded) return {};
    HMODULE gl = GetModuleHandleA("opengl32.dll");
    if (!gl) return std::unexpected("opengl32.dll is not loaded");
    auto gp = [gl](const char* name) { return reinterpret_cast<void*>(GetProcAddress(gl, name)); };

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

void GlApi::ResolveExtensions() {
    if (!glActiveTextureARB && wglGetProcAddress) {
        glActiveTextureARB = reinterpret_cast<decltype(glActiveTextureARB)>(
            reinterpret_cast<void*>(wglGetProcAddress("glActiveTextureARB")));
        if (!glActiveTextureARB) {
            glActiveTextureARB = reinterpret_cast<decltype(glActiveTextureARB)>(
                reinterpret_cast<void*>(wglGetProcAddress("glActiveTexture")));
        }
    }
}

FontAtlas::~FontAtlas() { DestroyGdi(); }

std::expected<void, std::string> FontAtlas::EnsureReady(GlApi& gl, HGLRC ctx, const std::wstring& faceName, int tall,
                                                        bool italic, bool bold) {
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

    std::vector<uint8_t> atlasRgba(static_cast<size_t>(kAtlasSize) * kAtlasSize * 4, 0);
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

const Glyph* FontAtlas::GetGlyph(GlApi& gl, wchar_t ch) {
    auto it = glyphs_.find(ch);
    if (it != glyphs_.end()) return &it->second;
    return RasterizeAndUpload(gl, ch);
}

int FontAtlas::MeasureWidth(GlApi& gl, const std::wstring& s) {
    int w = 0;
    for (wchar_t ch : s) {
        if (ch == L'\r' || ch == L'\n') continue;
        const Glyph* g = GetGlyph(gl, ch);
        if (g) w += g->advance;
    }
    return w;
}

void FontAtlas::DrawText(GlApi& gl, int x, int y, const std::wstring& s, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                         bool shadow, bool glow,
                         uint8_t gr, uint8_t gg, uint8_t gb, uint8_t ga) {
    if (s.empty() || !tex_) return;
    gl.glBindTexture(GlApi::kTexture2D, tex_);
    gl.glBegin(GlApi::kQuads);

    if (glow && ga > 0) {
        uint8_t outerA = static_cast<uint8_t>((static_cast<int>(ga) * static_cast<int>(a)) / 510);
        uint8_t innerA = static_cast<uint8_t>((static_cast<int>(ga) * static_cast<int>(a)) / 255);
        if (outerA > 0) {
            gl.glColor4ub(gr, gg, gb, outerA);
            static constexpr int kDx2[8] = {-2, 0, 2, -2, 2, -2, 0, 2};
            static constexpr int kDy2[8] = {-2, -2, -2, 0, 0, 2, 2, 2};
            for (int k = 0; k < 8; ++k) {
                EmitStringQuads(gl, static_cast<float>(x + kDx2[k]), static_cast<float>(y + kDy2[k]), s);
            }
        }
        gl.glColor4ub(gr, gg, gb, innerA);
        static constexpr int kDx1[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
        static constexpr int kDy1[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
        for (int k = 0; k < 8; ++k) {
            EmitStringQuads(gl, static_cast<float>(x + kDx1[k]), static_cast<float>(y + kDy1[k]), s);
        }
    } else if (shadow) {
        gl.glColor4ub(0, 0, 0, a);
        EmitStringQuads(gl, static_cast<float>(x + 1), static_cast<float>(y + 1), s);
    }

    gl.glColor4ub(r, g, b, a);
    EmitStringQuads(gl, static_cast<float>(x), static_cast<float>(y), s);

    gl.glEnd();
}

void FontAtlas::Invalidate() noexcept {
    tex_ = 0;
    ctx_ = nullptr;
}

std::expected<void, std::string> FontAtlas::InitGdi(const std::wstring& faceName, int tall,
                                                    bool italic, bool bold) {
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

    oldFont_ = static_cast<HFONT>(SelectObject(hdc_, hfont_));
    TEXTMETRICW tm{};
    GetTextMetricsW(hdc_, &tm);
    lineH_ = std::max(14, static_cast<int>(tm.tmHeight));
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
    oldBmp_ = static_cast<HBITMAP>(SelectObject(hdc_, dib_));

    SetBkMode(hdc_, TRANSPARENT);
    SetTextColor(hdc_, RGB(255, 255, 255));
    SetTextAlign(hdc_, TA_LEFT | TA_TOP);
    return {};
}

void FontAtlas::DestroyGdi() {
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

bool FontAtlas::RasterizeGlyphPixels(wchar_t ch, int& outAdv, int& outW, int& outH, std::vector<uint8_t>& outRgba) {
    if (!hdc_ || !dibBits_) return false;
    SIZE sz{};
    GetTextExtentPoint32W(hdc_, &ch, 1, &sz);
    outAdv = std::max(0, static_cast<int>(sz.cx));
    outW = std::clamp(outAdv + kGlyphPad * 2, 1, scratchW_);
    outH = std::min(lineH_ + kTopPad + 2, scratchH_);

    std::memset(dibBits_, 0, static_cast<size_t>(scratchW_) * scratchH_ * 4);
    ExtTextOutW(hdc_, kGlyphPad, kTopPad, 0, nullptr, &ch, 1, nullptr);
    GdiFlush();

    outRgba.resize(static_cast<size_t>(outW) * outH * 4);
    const uint8_t* src = static_cast<const uint8_t*>(dibBits_);
    for (int row = 0; row < outH; ++row) {
        const uint8_t* srow = src + static_cast<size_t>(row) * scratchW_ * 4;
        uint8_t* drow = outRgba.data() + static_cast<size_t>(row) * outW * 4;
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

bool FontAtlas::AllocAtlasSlot(int w, int h, int& outX, int& outY) {
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

void FontAtlas::StoreGlyph(wchar_t ch, int ax, int ay, int adv, int w, int h) {
    Glyph g;
    g.u0 = static_cast<float>(ax) / static_cast<float>(kAtlasSize);
    g.v0 = static_cast<float>(ay) / static_cast<float>(kAtlasSize);
    g.u1 = static_cast<float>(ax + w) / static_cast<float>(kAtlasSize);
    g.v1 = static_cast<float>(ay + h) / static_cast<float>(kAtlasSize);
    g.xOff = -kGlyphPad;
    g.yOff = -kTopPad;
    g.w = w;
    g.h = h;
    g.advance = adv;
    glyphs_[ch] = g;
}

void FontAtlas::RasterizeIntoBuffer(wchar_t ch, std::span<uint8_t> atlasRgba) {
    if (glyphs_.count(ch)) return;
    int adv = 0, w = 0, h = 0;
    std::vector<uint8_t> tmp;
    if (!RasterizeGlyphPixels(ch, adv, w, h, tmp)) return;
    int ax = 0, ay = 0;
    if (!AllocAtlasSlot(w, h, ax, ay)) return;
    std::span<const uint8_t> src(tmp);
    for (int row = 0; row < h; ++row) {
        auto srcRow = src.subspan(static_cast<size_t>(row) * w * 4, static_cast<size_t>(w) * 4);
        auto dstRow = atlasRgba.subspan((static_cast<size_t>(ay + row) * kAtlasSize + ax) * 4, static_cast<size_t>(w) * 4);
        std::ranges::copy(srcRow, dstRow.begin());
    }
    StoreGlyph(ch, ax, ay, adv, w, h);
}

const Glyph* FontAtlas::RasterizeAndUpload(GlApi& gl, wchar_t ch) {
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

void FontAtlas::EmitStringQuads(GlApi& gl, float startX, float startY, const std::wstring& s) {
    float cx = startX;
    for (wchar_t ch : s) {
        if (ch == L'\r' || ch == L'\n') continue;
        auto it = glyphs_.find(ch);
        if (it == glyphs_.end()) continue;
        const Glyph& g = it->second;
        float x0 = cx + static_cast<float>(g.xOff);
        float y0 = startY + static_cast<float>(g.yOff);
        float x1 = x0 + static_cast<float>(g.w);
        float y1 = y0 + static_cast<float>(g.h);
        gl.glTexCoord2f(g.u0, g.v0); gl.glVertex2f(x0, y0);
        gl.glTexCoord2f(g.u1, g.v0); gl.glVertex2f(x1, y0);
        gl.glTexCoord2f(g.u1, g.v1); gl.glVertex2f(x1, y1);
        gl.glTexCoord2f(g.u0, g.v1); gl.glVertex2f(x0, y1);
        cx += static_cast<float>(g.advance);
    }
}

} // namespace glfont
