#pragma once

/**
 * @file hud_fbo_scaler.hpp
 * @brief Scales the vanilla GoldSrc HUD cleanly via an offscreen OpenGL FBO.
 *
 * HUD_Redraw renders into a transparent RGBA8 FBO at native screen resolution,
 * then composites the 4 screen-corner HUD regions + middle-right pickup strip
 * scaled by hud_scale anchored to their respective corners while leaving the
 * center crosshair 1:1.
 */

#include <windows.h>

#include "render/gl_font.hpp"

namespace hud_fbo {

struct FboApi {
    static constexpr unsigned int kFramebuffer         = 0x8D40;
    static constexpr unsigned int kFramebufferBinding  = 0x8CA6;
    static constexpr unsigned int kColorAttachment0    = 0x8CE0;
    static constexpr unsigned int kFramebufferComplete = 0x8CD5;
    static constexpr unsigned int kClampToEdge         = 0x812F;
    static constexpr unsigned int kOne                 = 1;
    static constexpr unsigned int kOneMinusSrcAlpha    = 0x0303;
    static constexpr unsigned int kColorBufferBit      = 0x00004000;

    void(APIENTRY* glGenFramebuffers)(int, unsigned int*) = nullptr;
    void(APIENTRY* glDeleteFramebuffers)(int, const unsigned int*) = nullptr;
    void(APIENTRY* glBindFramebuffer)(unsigned int, unsigned int) = nullptr;
    void(APIENTRY* glFramebufferTexture2D)(unsigned int, unsigned int, unsigned int, unsigned int, int) = nullptr;
    unsigned int(APIENTRY* glCheckFramebufferStatus)(unsigned int) = nullptr;
    void(APIENTRY* glGetIntegerv)(unsigned int, int*) = nullptr;
    void(APIENTRY* glClearColor)(float, float, float, float) = nullptr;
    void(APIENTRY* glClear)(unsigned int) = nullptr;

    bool tried = false;
    bool ok = false;

    bool EnsureLoaded(const glfont::GlApi& gl);
};

class HudFboScaler {
public:
    bool BeginCapture(const glfont::GlApi& gl, HGLRC ctx, int w, int h);
    void EndCapture();
    bool IsCapturing() const noexcept { return capturing_; }
    void PauseCapture();
    void ResumeCapture();
    void CompositeScaled(const glfont::GlApi& gl, int w, int h, float scale);

private:
    bool EnsureResources(const glfont::GlApi& gl, HGLRC ctx, int w, int h);

    FboApi api_{};
    HGLRC ctx_ = nullptr;
    unsigned int fbo_ = 0;
    unsigned int tex_ = 0;
    int texW_ = 0;
    int texH_ = 0;
    int prevFbo_ = 0;
    bool capturing_ = false;
    bool failed_ = false;
};

} // namespace hud_fbo
