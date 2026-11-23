#pragma once

#include <windows.h>

#include <cstdint>
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

    std::expected<void, std::string> Load();
    void ResolveExtensions();
};

// Self-contained GDI-rasterized OpenGL font atlas, independent of VGUI2.
class FontAtlas {
public:
    static constexpr int kAtlasSize = 1024;
    static constexpr int kGlyphPad = 6;
    static constexpr int kTopPad = 2;

    ~FontAtlas();

    std::expected<void, std::string> EnsureReady(GlApi& gl, HGLRC ctx, const std::wstring& faceName, int tall,
                                                 bool italic = false, bool bold = true);

    int LineHeight() const noexcept { return lineH_; }
    unsigned int Texture() const noexcept { return tex_; }

    const Glyph* GetGlyph(GlApi& gl, wchar_t ch);
    int MeasureWidth(GlApi& gl, const std::wstring& s);
    void DrawText(GlApi& gl, int x, int y, const std::wstring& s, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                  bool shadow = true, bool glow = false,
                  uint8_t gr = 255, uint8_t gg = 195, uint8_t gb = 55, uint8_t ga = 55);
    void Invalidate() noexcept;

private:
    std::expected<void, std::string> InitGdi(const std::wstring& faceName, int tall,
                                             bool italic = false, bool bold = true);
    void DestroyGdi();
    bool RasterizeGlyphPixels(wchar_t ch, int& outAdv, int& outW, int& outH, std::vector<uint8_t>& outRgba);
    bool AllocAtlasSlot(int w, int h, int& outX, int& outY);
    void StoreGlyph(wchar_t ch, int ax, int ay, int adv, int w, int h);
    void RasterizeIntoBuffer(wchar_t ch, std::span<uint8_t> atlasRgba);
    const Glyph* RasterizeAndUpload(GlApi& gl, wchar_t ch);
    void EmitStringQuads(GlApi& gl, float startX, float startY, const std::wstring& s);

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
