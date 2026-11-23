#pragma once

/**
 * @file flashlight_gl.hpp
 * @brief Self-contained OpenGL 3D volumetric projected flashlight (HL2 / Source style).
 *
 * Completely isolated rendering plugin: depends only on render/gl_font.hpp and core/log.hpp.
 */

#include <windows.h>

#include "render/gl_font.hpp"

namespace gl_flash {

struct GlFlashApi {
    using GLhandle = unsigned int;
    using GLchar   = char;

    static constexpr unsigned int kFragmentShader   = 0x8B30;
    static constexpr unsigned int kVertexShader     = 0x8B31;
    static constexpr unsigned int kCompileStatus    = 0x8B81;
    static constexpr unsigned int kLinkStatus       = 0x8B82;
    static constexpr unsigned int kCurrentProgram   = 0x8B8D;
    static constexpr unsigned int kDepthComponent24 = 0x81A6;
    static constexpr unsigned int kDepthComponent   = 0x1902;
    static constexpr unsigned int kDepth24Stencil8  = 0x88F0;
    static constexpr unsigned int kDepthStencil     = 0x84F9;
    static constexpr unsigned int kUnsignedInt248   = 0x84FA;
    static constexpr unsigned int kFramebuffer      = 0x8D40;
    static constexpr unsigned int kReadFramebuffer  = 0x8CA8;
    static constexpr unsigned int kDrawFramebuffer  = 0x8CA9;
    static constexpr unsigned int kFramebufferBind  = 0x8CA6;
    static constexpr unsigned int kColorAttachment0 = 0x8CE0;
    static constexpr unsigned int kDepthAttachment  = 0x8D00;
    static constexpr unsigned int kDepthStencilAtt  = 0x821A;
    static constexpr unsigned int kFboComplete      = 0x8CD5;
    static constexpr unsigned int kColorBufferBit   = 0x00004000;
    static constexpr unsigned int kDepthBufferBit   = 0x00000100;
    static constexpr unsigned int kClampToEdge      = 0x812F;
    static constexpr unsigned int kNearest          = 0x2600;
    static constexpr unsigned int kUnsignedInt      = 0x1405;
    static constexpr unsigned int kOne              = 1;

    GLhandle(APIENTRY* glCreateShader)(unsigned int) = nullptr;
    void(APIENTRY* glShaderSource)(GLhandle, int, const GLchar**, const int*) = nullptr;
    void(APIENTRY* glCompileShader)(GLhandle) = nullptr;
    void(APIENTRY* glGetShaderiv)(GLhandle, unsigned int, int*) = nullptr;
    void(APIENTRY* glGetShaderInfoLog)(GLhandle, int, int*, GLchar*) = nullptr;
    void(APIENTRY* glDeleteShader)(GLhandle) = nullptr;
    GLhandle(APIENTRY* glCreateProgram)() = nullptr;
    void(APIENTRY* glAttachShader)(GLhandle, GLhandle) = nullptr;
    void(APIENTRY* glLinkProgram)(GLhandle) = nullptr;
    void(APIENTRY* glGetProgramiv)(GLhandle, unsigned int, int*) = nullptr;
    void(APIENTRY* glGetProgramInfoLog)(GLhandle, int, int*, GLchar*) = nullptr;
    void(APIENTRY* glDeleteProgram)(GLhandle) = nullptr;
    void(APIENTRY* glUseProgram)(GLhandle) = nullptr;
    int(APIENTRY* glGetUniformLocation)(GLhandle, const GLchar*) = nullptr;
    void(APIENTRY* glUniform1i)(int, int) = nullptr;
    void(APIENTRY* glUniform1f)(int, float) = nullptr;
    void(APIENTRY* glUniform2f)(int, float, float) = nullptr;
    void(APIENTRY* glCopyTexSubImage2D)(unsigned int, int, int, int, int, int, int, int) = nullptr;
    void(APIENTRY* glActiveTexture)(unsigned int) = nullptr;
    void(APIENTRY* glGetIntegerv)(unsigned int, int*) = nullptr;
    void(APIENTRY* glReadPixels)(int, int, int, int, unsigned int, unsigned int, void*) = nullptr;
    unsigned int(APIENTRY* glGetError)() = nullptr;
    void(APIENTRY* glGenFramebuffers)(int, unsigned int*) = nullptr;
    void(APIENTRY* glDeleteFramebuffers)(int, const unsigned int*) = nullptr;
    void(APIENTRY* glBindFramebuffer)(unsigned int, unsigned int) = nullptr;
    void(APIENTRY* glFramebufferTexture2D)(unsigned int, unsigned int, unsigned int, unsigned int, int) = nullptr;
    unsigned int(APIENTRY* glCheckFramebufferStatus)(unsigned int) = nullptr;
    void(APIENTRY* glBlitFramebuffer)(int, int, int, int, int, int, int, int, unsigned int, unsigned int) = nullptr;

    bool triedLoad = false;
    bool ok = false;

    bool EnsureLoaded(const glfont::GlApi& gl);
};

class GlFlashlight {
public:
    bool IsReady() const noexcept { return api_.ok && prog_ != 0 && !shaderFailed_; }
    bool HasFailed() const noexcept { return shaderFailed_; }
    bool HasLoggedDiag() const noexcept { return loggedDepthOnce_; }

    void UpdateSway(float pitchDeg, float yawDeg, float dtSec);
    void ResetSway() noexcept;
    void Render(const glfont::GlApi& gl, HGLRC ctx, int w, int h, float intensityScale);

private:
    void AttachResolveFbo(const glfont::GlApi& gl, int w, int h);
    bool EnsureResources(const glfont::GlApi& gl, HGLRC ctx, int w, int h);
    bool CompileShaderProgram();

    GlFlashApi api_{};
    HGLRC ctx_ = nullptr;
    GlFlashApi::GLhandle prog_ = 0;
    unsigned int colorTex_ = 0;
    unsigned int depthTex_ = 0;
    unsigned int resolveFbo_ = 0;
    bool useDepthStencil_ = false;
    int texW_ = 0;
    int texH_ = 0;
    int uColor_ = -1;
    int uDepth_ = -1;
    int uInvRes_ = -1;
    int uAspect_ = -1;
    int uSway_ = -1;
    int uIntensity_ = -1;
    bool shaderFailed_ = false;
    bool loggedDepthOnce_ = false;

    bool hasPrevAngles_ = false;
    float prevPitch_ = 0.0f;
    float prevYaw_ = 0.0f;
    float swayX_ = 0.0f;
    float swayY_ = 0.0f;
};

} // namespace gl_flash
