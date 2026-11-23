#include "render/hud_fbo_scaler.hpp"

#include <algorithm>

#include "core/log.hpp"

namespace hud_fbo {

bool FboApi::EnsureLoaded(const glfont::GlApi& gl) {
    if (tried) return ok;
    if (!gl.wglGetCurrentContext || !gl.wglGetCurrentContext()) return false;
    tried = true;
    HMODULE hm = GetModuleHandleA("opengl32.dll");
    auto gp = [&](const char* name, const char* extName = nullptr) -> void* {
        void* p = gl.wglGetProcAddress ? reinterpret_cast<void*>(gl.wglGetProcAddress(name)) : nullptr;
        if (!p && extName && gl.wglGetProcAddress) p = reinterpret_cast<void*>(gl.wglGetProcAddress(extName));
        if (!p && hm) p = reinterpret_cast<void*>(GetProcAddress(hm, name));
        return p;
    };
    glGenFramebuffers        = reinterpret_cast<decltype(glGenFramebuffers)>(gp("glGenFramebuffers", "glGenFramebuffersEXT"));
    glDeleteFramebuffers     = reinterpret_cast<decltype(glDeleteFramebuffers)>(gp("glDeleteFramebuffers", "glDeleteFramebuffersEXT"));
    glBindFramebuffer        = reinterpret_cast<decltype(glBindFramebuffer)>(gp("glBindFramebuffer", "glBindFramebufferEXT"));
    glFramebufferTexture2D   = reinterpret_cast<decltype(glFramebufferTexture2D)>(gp("glFramebufferTexture2D", "glFramebufferTexture2DEXT"));
    glCheckFramebufferStatus = reinterpret_cast<decltype(glCheckFramebufferStatus)>(gp("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT"));
    glGetIntegerv            = reinterpret_cast<decltype(glGetIntegerv)>(gp("glGetIntegerv"));
    glClearColor             = reinterpret_cast<decltype(glClearColor)>(gp("glClearColor"));
    glClear                  = reinterpret_cast<decltype(glClear)>(gp("glClear"));

    ok = (glGenFramebuffers && glDeleteFramebuffers && glBindFramebuffer &&
          glFramebufferTexture2D && glCheckFramebufferStatus && glGetIntegerv &&
          glClearColor && glClear);
    if (!ok) {
        core::LogMsg("[hud-fbo] Missing OpenGL FBO support, falling back to native HUD");
    }
    return ok;
}

bool HudFboScaler::BeginCapture(const glfont::GlApi& gl, HGLRC ctx, int w, int h) {
    if (w <= 0 || h <= 0 || !ctx) return false;
    if (!api_.EnsureLoaded(gl)) return false;
    if (!EnsureResources(gl, ctx, w, h)) return false;

    api_.glGetIntegerv(FboApi::kFramebufferBinding, &prevFbo_);
    api_.glBindFramebuffer(FboApi::kFramebuffer, fbo_);
    api_.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    api_.glClear(FboApi::kColorBufferBit);
    capturing_ = true;
    return true;
}

void HudFboScaler::EndCapture() {
    if (!capturing_) return;
    api_.glBindFramebuffer(FboApi::kFramebuffer, static_cast<unsigned int>(prevFbo_));
    capturing_ = false;
}

void HudFboScaler::PauseCapture() {
    if (capturing_) api_.glBindFramebuffer(FboApi::kFramebuffer, static_cast<unsigned int>(prevFbo_));
}

void HudFboScaler::ResumeCapture() {
    if (capturing_) api_.glBindFramebuffer(FboApi::kFramebuffer, fbo_);
}

void HudFboScaler::CompositeScaled(const glfont::GlApi& gl, int w, int h, float scale) {
    if (failed_ || !tex_ || w <= 0 || h <= 0) return;

    gl.glPushAttrib(glfont::GlApi::kAllAttribBits);
    gl.glPushClientAttrib(glfont::GlApi::kClientAllAttribBits);

    if (gl.glActiveTextureARB) {
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
    gl.glBlendFunc(FboApi::kOne, FboApi::kOne);
    gl.glEnable(glfont::GlApi::kTexture2D);
    gl.glBindTexture(glfont::GlApi::kTexture2D, tex_);
    gl.glTexEnvi(glfont::GlApi::kTextureEnv, glfont::GlApi::kTextureEnvMode, glfont::GlApi::kModulate);

    gl.glMatrixMode(glfont::GlApi::kProjection);
    gl.glPushMatrix();
    gl.glLoadIdentity();
    gl.glOrtho(0.0, static_cast<double>(w), static_cast<double>(h), 0.0, -1.0, 1.0);
    gl.glMatrixMode(glfont::GlApi::kModelview);
    gl.glPushMatrix();
    gl.glLoadIdentity();

    gl.glColor4ub(255, 255, 255, 255);

    auto drawRegion = [&](float sx0, float sy0, float sx1, float sy1, float ax, float ay, float s) {
        float u0 = sx0, u1 = sx1;
        float v0 = 1.0f - sy0, v1 = 1.0f - sy1;
        float x0 = ax + (sx0 * static_cast<float>(w) - ax) * s;
        float x1 = ax + (sx1 * static_cast<float>(w) - ax) * s;
        float y0 = ay + (sy0 * static_cast<float>(h) - ay) * s;
        float y1 = ay + (sy1 * static_cast<float>(h) - ay) * s;
        gl.glBegin(glfont::GlApi::kQuads);
        gl.glTexCoord2f(u0, v0); gl.glVertex2f(x0, y0);
        gl.glTexCoord2f(u1, v0); gl.glVertex2f(x1, y0);
        gl.glTexCoord2f(u1, v1); gl.glVertex2f(x1, y1);
        gl.glTexCoord2f(u0, v1); gl.glVertex2f(x0, y1);
        gl.glEnd();
    };

    // 1. Bottom-left (Health & Armor)
    drawRegion(0.0f,  0.85f, 0.26f, 1.0f,  0.0f,                  static_cast<float>(h), scale);
    // 2. Bottom-right (Ammo)
    drawRegion(0.76f, 0.85f, 1.0f,  1.0f,  static_cast<float>(w), static_cast<float>(h), scale);
    // 3. Top-left (Weapon menu, chat)
    drawRegion(0.0f,  0.0f,  0.46f, 0.30f, 0.0f,                  0.0f,                  scale);
    // 4. Top-right (Flashlight)
    drawRegion(0.80f, 0.0f,  1.0f,  0.20f, static_cast<float>(w), 0.0f,                  scale);
    // 5. Middle-right (Pickup history): seamless strip above Bottom-right, anchored at (w, h)
    const float trY    = 0.20f * scale;
    const float mrSrcY = std::max(0.55f, 1.0f - (1.0f - trY) / scale);
    if (mrSrcY < 0.85f) {
        drawRegion(0.84f, mrSrcY, 1.0f, 0.85f, static_cast<float>(w), static_cast<float>(h), scale);
    }

    // 6. Remaining uncovered center/gap regions (1:1 unscaled, non-overlapping)
    const float tlX = 0.46f * scale;
    const float tlY = 0.30f * scale;
    const float trX = 1.0f - 0.20f * scale;
    const float mrX = 1.0f - 0.16f * scale;
    const float mrY = (mrSrcY < 0.85f) ? (1.0f - (1.0f - mrSrcY) * scale) : (1.0f - 0.15f * scale);
    const float blX = 0.26f * scale;
    const float blY = 1.0f - 0.15f * scale;
    const float brX = 1.0f - 0.24f * scale;

    auto drawUnscaled = [&](float x0, float y0, float x1, float y1) {
        if (x0 < x1 && y0 < y1) drawRegion(x0, y0, x1, y1, 0.0f, 0.0f, 1.0f);
    };
    const float yA = std::min(tlY, mrY);
    const float yB = std::max(tlY, mrY);
    drawUnscaled(tlX, 0.0f, trX, trY);
    drawUnscaled(tlX, trY, 1.0f, yA);
    if (mrY <= tlY) {
        drawUnscaled(tlX, yA, mrX, yB);
    } else {
        drawUnscaled(0.0f, yA, 1.0f, yB);
    }
    drawUnscaled(0.0f, yB, mrX, blY);
    drawUnscaled(blX, blY, brX, 1.0f);

    gl.glBindTexture(glfont::GlApi::kTexture2D, 0);
    gl.glMatrixMode(glfont::GlApi::kModelview);
    gl.glPopMatrix();
    gl.glMatrixMode(glfont::GlApi::kProjection);
    gl.glPopMatrix();

    gl.glPopClientAttrib();
    gl.glPopAttrib();
}

bool HudFboScaler::EnsureResources(const glfont::GlApi& gl, HGLRC ctx, int w, int h) {
    if (ctx != ctx_) {
        ctx_ = ctx;
        fbo_ = 0;
        tex_ = 0;
        texW_ = 0;
        texH_ = 0;
        failed_ = false;
    }
    if (failed_) return false;
    if (texW_ == w && texH_ == h && fbo_ != 0 && tex_ != 0) return true;

    if (tex_) gl.glDeleteTextures(1, &tex_);
    if (fbo_) api_.glDeleteFramebuffers(1, &fbo_);

    gl.glGenTextures(1, &tex_);
    gl.glBindTexture(glfont::GlApi::kTexture2D, tex_);
    gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexMinFilter, glfont::GlApi::kLinear);
    gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexMagFilter, glfont::GlApi::kLinear);
    gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexWrapS, FboApi::kClampToEdge);
    gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexWrapT, FboApi::kClampToEdge);
    gl.glTexImage2D(glfont::GlApi::kTexture2D, 0, glfont::GlApi::kRgba, w, h, 0,
                    glfont::GlApi::kRgba, glfont::GlApi::kUnsignedByte, nullptr);
    gl.glBindTexture(glfont::GlApi::kTexture2D, 0);

    int prev = 0;
    api_.glGetIntegerv(FboApi::kFramebufferBinding, &prev);
    api_.glGenFramebuffers(1, &fbo_);
    api_.glBindFramebuffer(FboApi::kFramebuffer, fbo_);
    api_.glFramebufferTexture2D(FboApi::kFramebuffer, FboApi::kColorAttachment0,
                                glfont::GlApi::kTexture2D, tex_, 0);
    unsigned int status = api_.glCheckFramebufferStatus(FboApi::kFramebuffer);
    api_.glBindFramebuffer(FboApi::kFramebuffer, static_cast<unsigned int>(prev));
    if (status != FboApi::kFramebufferComplete) {
        core::LogMsg("[hud-fbo] FBO incomplete: 0x%X", status);
        failed_ = true;
        return false;
    }
    texW_ = w;
    texH_ = h;
    core::LogMsg("[hud-fbo] Created %dx%d HUD FBO", w, h);
    return true;
}

} // namespace hud_fbo
