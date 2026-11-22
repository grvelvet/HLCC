#pragma once

/**
 * @file flashlight_gl.hpp
 * @brief Experimental OpenGL 3D volumetric projected flashlight (HL2 / Source style).
 *
 * Pipeline (runs in Hook_WglSwapBuffers before HUD/subtitles):
 *  1. Copies backbuffer color (GL_RGBA8) and depth (GL_DEPTH_COMPONENT24) to textures.
 *  2. Reconstructs 3D view-space coordinates per pixel using GoldSrc's perspective frustum.
 *  3. Estimates edge-aware surface normals via 4-tap depth derivatives (avoids dark edge halos).
 *  4. Projects a procedural HL2-style lens cookie (warm bright core, subtle lens ring,
 *     cool soft penumbra) with smooth spring-damped inertial sway from camera rotation.
 *  5. Uses gamma-lifted albedo illumination so dark GoldSrc corridors reveal actual wall
 *     texture detail instead of milky gray washout, while excluding skybox and softening
 *     near-camera viewmodel geometry.
 *
 * Configured via [HUD] in subtitles.ini:
 *   enable_gl_flashlight=1
 *   gl_flashlight_intensity=1.0
 *   gl_flashlight_hide_vanilla=1
 */

#include "app_state.hpp"

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

    bool triedLoad = false;
    bool ok = false;

    bool EnsureLoaded(const glfont::GlApi& gl) {
        if (triedLoad) return ok;
        if (!gl.wglGetProcAddress || !gl.wglGetCurrentContext || !gl.wglGetCurrentContext()) return false;
        triedLoad = true;
        HMODULE hm = GetModuleHandleA("opengl32.dll");
        auto gp = [&](const char* name) -> void* {
            void* p = reinterpret_cast<void*>(gl.wglGetProcAddress(name));
            if (!p && hm) p = reinterpret_cast<void*>(GetProcAddress(hm, name));
            return p;
        };

        glCreateShader       = reinterpret_cast<decltype(glCreateShader)>(gp("glCreateShader"));
        glShaderSource       = reinterpret_cast<decltype(glShaderSource)>(gp("glShaderSource"));
        glCompileShader      = reinterpret_cast<decltype(glCompileShader)>(gp("glCompileShader"));
        glGetShaderiv        = reinterpret_cast<decltype(glGetShaderiv)>(gp("glGetShaderiv"));
        glGetShaderInfoLog   = reinterpret_cast<decltype(glGetShaderInfoLog)>(gp("glGetShaderInfoLog"));
        glDeleteShader       = reinterpret_cast<decltype(glDeleteShader)>(gp("glDeleteShader"));
        glCreateProgram      = reinterpret_cast<decltype(glCreateProgram)>(gp("glCreateProgram"));
        glAttachShader       = reinterpret_cast<decltype(glAttachShader)>(gp("glAttachShader"));
        glLinkProgram        = reinterpret_cast<decltype(glLinkProgram)>(gp("glLinkProgram"));
        glGetProgramiv       = reinterpret_cast<decltype(glGetProgramiv)>(gp("glGetProgramiv"));
        glGetProgramInfoLog  = reinterpret_cast<decltype(glGetProgramInfoLog)>(gp("glGetProgramInfoLog"));
        glDeleteProgram      = reinterpret_cast<decltype(glDeleteProgram)>(gp("glDeleteProgram"));
        glUseProgram         = reinterpret_cast<decltype(glUseProgram)>(gp("glUseProgram"));
        glGetUniformLocation = reinterpret_cast<decltype(glGetUniformLocation)>(gp("glGetUniformLocation"));
        glUniform1i          = reinterpret_cast<decltype(glUniform1i)>(gp("glUniform1i"));
        glUniform1f          = reinterpret_cast<decltype(glUniform1f)>(gp("glUniform1f"));
        glUniform2f          = reinterpret_cast<decltype(glUniform2f)>(gp("glUniform2f"));
        glCopyTexSubImage2D  = reinterpret_cast<decltype(glCopyTexSubImage2D)>(gp("glCopyTexSubImage2D"));
        glActiveTexture      = reinterpret_cast<decltype(glActiveTexture)>(gp("glActiveTexture"));
        if (!glActiveTexture) glActiveTexture = reinterpret_cast<decltype(glActiveTexture)>(gp("glActiveTextureARB"));
        glGetIntegerv        = reinterpret_cast<decltype(glGetIntegerv)>(gp("glGetIntegerv"));
        glReadPixels         = reinterpret_cast<decltype(glReadPixels)>(gp("glReadPixels"));
        glGetError           = reinterpret_cast<decltype(glGetError)>(gp("glGetError"));

        ok = (glCreateShader && glShaderSource && glCompileShader && glGetShaderiv &&
              glCreateProgram && glAttachShader && glLinkProgram && glGetProgramiv &&
              glUseProgram && glGetUniformLocation && glUniform1i && glUniform1f &&
              glUniform2f && glCopyTexSubImage2D && glActiveTexture);
        if (!ok) {
            LogMsg("[gl-flash] Missing GL procs: cs=%d cp=%d copy=%d act=%d",
                   (int)(glCreateShader != nullptr), (int)(glCreateProgram != nullptr),
                   (int)(glCopyTexSubImage2D != nullptr), (int)(glActiveTexture != nullptr));
        }
        return ok;
    }
};

class GlFlashlight {
public:
    bool IsReady() const noexcept {
        return api_.ok && prog_ != 0 && !shaderFailed_;
    }

    bool HasFailed() const noexcept { return shaderFailed_; }

    bool HasLoggedDiag() const noexcept { return loggedDepthOnce_; }

    // Updates smooth spring-damped inertial beam sway from camera angles (called in V_CalcRefdef)
    void UpdateSway(float pitchDeg, float yawDeg, float dtSec) {
        if (!hasPrevAngles_) {
            prevPitch_ = pitchDeg;
            prevYaw_ = yawDeg;
            hasPrevAngles_ = true;
            return;
        }
        float dYaw = yawDeg - prevYaw_;
        while (dYaw > 180.0f) dYaw -= 360.0f;
        while (dYaw < -180.0f) dYaw += 360.0f;
        float dPitch = pitchDeg - prevPitch_;
        prevPitch_ = pitchDeg;
        prevYaw_ = yawDeg;

        // Beam lags slightly behind rapid camera rotation, then smoothly recenters
        swayX_ = std::clamp(swayX_ + dYaw * 0.0022f, -0.055f, 0.055f);
        swayY_ = std::clamp(swayY_ + dPitch * 0.0022f, -0.045f, 0.045f);
        float decay = std::exp(-std::clamp(dtSec, 0.001f, 0.1f) * 11.0f);
        swayX_ *= decay;
        swayY_ *= decay;
    }

    void ResetSway() noexcept {
        hasPrevAngles_ = false;
        swayX_ = 0.0f;
        swayY_ = 0.0f;
    }

    void Render(const glfont::GlApi& gl, HGLRC ctx, int w, int h, float intensityScale) {
        if (w <= 0 || h <= 0 || !ctx) return;
        if (!api_.EnsureLoaded(gl)) return;
        if (!EnsureResources(gl, ctx, w, h)) return;

        int prevProg = 0;
        if (api_.glGetIntegerv) {
            api_.glGetIntegerv(GlFlashApi::kCurrentProgram, &prevProg);
        }

        gl.glPushAttrib(glfont::GlApi::kAllAttribBits);
        gl.glPushClientAttrib(glfont::GlApi::kClientAllAttribBits);

        gl.glDisable(glfont::GlApi::kDepthTest);
        gl.glDisable(glfont::GlApi::kCullFace);
        gl.glDisable(glfont::GlApi::kScissorTest);
        gl.glDisable(glfont::GlApi::kAlphaTest);
        gl.glDisable(glfont::GlApi::kLighting);
        gl.glDisable(glfont::GlApi::kFog);
        gl.glDepthMask(0);

        // Copy backbuffer color & depth into dedicated screen textures
        api_.glActiveTexture(glfont::GlApi::kTexture0);
        gl.glEnable(glfont::GlApi::kTexture2D);
        gl.glBindTexture(glfont::GlApi::kTexture2D, colorTex_);
        api_.glCopyTexSubImage2D(glfont::GlApi::kTexture2D, 0, 0, 0, 0, 0, w, h);

        api_.glActiveTexture(glfont::GlApi::kTexture0 + 1);
        gl.glEnable(glfont::GlApi::kTexture2D);
        gl.glBindTexture(glfont::GlApi::kTexture2D, depthTex_);
        api_.glCopyTexSubImage2D(glfont::GlApi::kTexture2D, 0, 0, 0, 0, 0, w, h);

        if (!loggedDepthOnce_ && api_.glReadPixels && api_.glGetError) {
            loggedDepthOnce_ = true;
            float cd = -1.0f;
            api_.glReadPixels(w / 2, h / 2, 1, 1, GlFlashApi::kDepthComponent, 0x1406 /* GL_FLOAT */, &cd);
            int fb = -1;
            if (api_.glGetIntegerv) api_.glGetIntegerv(0x8CA6 /* GL_FRAMEBUFFER_BINDING */, &fb);
            LogMsg("[gl-flash] render diag: w=%d h=%d fb=%d prevProg=%d centerDepth=%.6f err=0x%X",
                   w, h, fb, prevProg, cd, api_.glGetError());
        }

        gl.glMatrixMode(glfont::GlApi::kProjection);
        gl.glPushMatrix();
        gl.glLoadIdentity();
        gl.glOrtho(0.0, (double)w, 0.0, (double)h, -1.0, 1.0);
        gl.glMatrixMode(glfont::GlApi::kModelview);
        gl.glPushMatrix();
        gl.glLoadIdentity();

        gl.glEnable(glfont::GlApi::kBlend);
        gl.glBlendFunc(GlFlashApi::kOne, GlFlashApi::kOne);

        api_.glUseProgram(prog_);
        api_.glUniform1i(uColor_, 0);
        api_.glUniform1i(uDepth_, 1);
        api_.glUniform2f(uInvRes_, 1.0f / (float)w, 1.0f / (float)h);
        api_.glUniform1f(uAspect_, (float)w / (float)h);
        api_.glUniform2f(uSway_, swayX_, swayY_);
        api_.glUniform1f(uIntensity_, std::clamp(intensityScale, 0.2f, 2.5f));

        gl.glColor4ub(255, 255, 255, 255);
        gl.glBegin(glfont::GlApi::kQuads);
        gl.glTexCoord2f(0.0f, 0.0f); gl.glVertex2f(0.0f, 0.0f);
        gl.glTexCoord2f(1.0f, 0.0f); gl.glVertex2f((float)w, 0.0f);
        gl.glTexCoord2f(1.0f, 1.0f); gl.glVertex2f((float)w, (float)h);
        gl.glTexCoord2f(0.0f, 1.0f); gl.glVertex2f(0.0f, (float)h);
        gl.glEnd();

        api_.glUseProgram(static_cast<GlFlashApi::GLhandle>(prevProg));

        api_.glActiveTexture(glfont::GlApi::kTexture0 + 1);
        gl.glBindTexture(glfont::GlApi::kTexture2D, 0);
        gl.glDisable(glfont::GlApi::kTexture2D);
        api_.glActiveTexture(glfont::GlApi::kTexture0);
        gl.glBindTexture(glfont::GlApi::kTexture2D, 0);

        gl.glMatrixMode(glfont::GlApi::kModelview);
        gl.glPopMatrix();
        gl.glMatrixMode(glfont::GlApi::kProjection);
        gl.glPopMatrix();

        gl.glPopClientAttrib();
        gl.glPopAttrib();
    }

private:
    bool EnsureResources(const glfont::GlApi& gl, HGLRC ctx, int w, int h) {
        if (ctx != ctx_) {
            ctx_ = ctx;
            prog_ = 0;
            colorTex_ = 0;
            depthTex_ = 0;
            texW_ = 0;
            texH_ = 0;
            shaderFailed_ = false;
        }
        if (shaderFailed_) return false;

        if (prog_ == 0) {
            if (!CompileShaderProgram()) {
                shaderFailed_ = true;
                return false;
            }
            LogMsg("[gl-flash] Shader program compiled successfully");
        }
        if (texW_ != w || texH_ != h || colorTex_ == 0 || depthTex_ == 0) {
            if (colorTex_) gl.glDeleteTextures(1, &colorTex_);
            if (depthTex_) gl.glDeleteTextures(1, &depthTex_);
            gl.glGenTextures(1, &colorTex_);
            gl.glGenTextures(1, &depthTex_);

            gl.glBindTexture(glfont::GlApi::kTexture2D, colorTex_);
            gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexMinFilter, GlFlashApi::kNearest);
            gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexMagFilter, GlFlashApi::kNearest);
            gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexWrapS, GlFlashApi::kClampToEdge);
            gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexWrapT, GlFlashApi::kClampToEdge);
            gl.glTexImage2D(glfont::GlApi::kTexture2D, 0, glfont::GlApi::kRgba, w, h, 0,
                            glfont::GlApi::kRgba, glfont::GlApi::kUnsignedByte, nullptr);

            gl.glBindTexture(glfont::GlApi::kTexture2D, depthTex_);
            gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexMinFilter, GlFlashApi::kNearest);
            gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexMagFilter, GlFlashApi::kNearest);
            gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexWrapS, GlFlashApi::kClampToEdge);
            gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexWrapT, GlFlashApi::kClampToEdge);
            gl.glTexImage2D(glfont::GlApi::kTexture2D, 0, GlFlashApi::kDepthComponent24, w, h, 0,
                            GlFlashApi::kDepthComponent, GlFlashApi::kUnsignedInt, nullptr);

            gl.glBindTexture(glfont::GlApi::kTexture2D, 0);
            texW_ = w;
            texH_ = h;
        }
        return true;
    }

    bool CompileShaderProgram() {
        static const char* kVertSrc = R"GLSL(#version 120
varying vec2 vUv;
void main() {
    vUv = gl_MultiTexCoord0.xy;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)GLSL";

        static const char* kFragSrc = R"GLSL(#version 120
uniform sampler2D uColor;
uniform sampler2D uDepth;
uniform vec2 uInvRes;
uniform float uAspect;
uniform vec2 uSway;
uniform float uIntensity;
varying vec2 vUv;

vec3 ViewPosFromDepth(vec2 uv, float d) {
    float zNear = 4.0;
    float zFar  = 4096.0;
    float z = (zNear * zFar) / max(0.0001, zFar - d * (zFar - zNear));
    vec2 ndc = uv * 2.0 - 1.0;
    float tanHalfY = 0.72;
    return vec3(ndc.x * tanHalfY * uAspect * z, ndc.y * tanHalfY * z, z);
}

void main() {
    float d = texture2D(uDepth, vUv).r;
    // Exclude skybox/far plane and zero-depth overlay pixels
    if (d >= 0.9988 || d <= 0.0001) {
        gl_FragColor = vec4(0.0);
        return;
    }

    vec3 pos = ViewPosFromDepth(vUv, d);

    // 4-tap edge-aware normal reconstruction (prevents dark outline halos on edges)
    vec2 stepX = vec2(uInvRes.x * 2.0, 0.0);
    vec2 stepY = vec2(0.0, uInvRes.y * 2.0);
    float dR = texture2D(uDepth, vUv + stepX).r;
    float dL = texture2D(uDepth, vUv - stepX).r;
    float dU = texture2D(uDepth, vUv + stepY).r;
    float dD = texture2D(uDepth, vUv - stepY).r;

    vec3 dPdx = (abs(dR - d) < abs(d - dL))
        ? (ViewPosFromDepth(vUv + stepX, dR) - pos)
        : (pos - ViewPosFromDepth(vUv - stepX, dL));
    vec3 dPdy = (abs(dU - d) < abs(d - dD))
        ? (ViewPosFromDepth(vUv + stepY, dU) - pos)
        : (pos - ViewPosFromDepth(vUv - stepY, dD));
    vec3 normal = normalize(cross(dPdy, dPdx));

    // Hand-held flashlight emitter offset + smooth inertial sway
    vec3 lightOrigin = vec3(6.5, -5.0, 0.0);
    vec3 lightDir = normalize(vec3(-0.014 + uSway.x, 0.010 + uSway.y, 1.0));
    vec3 toFrag = pos - lightOrigin;
    float dist = length(toFrag);
    vec3 dir = toFrag / max(dist, 0.001);

    float cosA = dot(dir, lightDir);
    // Procedural HL2/tactical lens cookie:
    //  - Hot inner core (~0..8 deg)
    //  - Mid body (~8..16 deg)
    //  - Subtle lens ring near rim (~14..17 deg)
    //  - Soft penumbra cutoff (~17..22 deg)
    float outerPenumbra = smoothstep(0.922, 0.962, cosA);
    float midBeam       = smoothstep(0.955, 0.982, cosA);
    float hotCore       = smoothstep(0.980, 0.996, cosA);
    float lensRing      = exp(-pow((cosA - 0.956) * 52.0, 2.0)) * 0.24 * outerPenumbra;
    float cookie        = outerPenumbra * 0.55 + midBeam * 0.45 + hotCore * 0.50 + lensRing;

    // Physically motivated inverse-square falloff with smooth max-range fade
    float invSq = 1.0 / (1.0 + 0.0013 * dist + 0.0000030 * dist * dist);
    float rangeFade = smoothstep(1150.0, 200.0, dist);
    float distAtten = invSq * rangeFade;

    // Surface angle shading (wrapped so glancing walls stay legible)
    float nDotL = mix(0.38, 1.0, clamp(abs(dot(normal, -dir)), 0.0, 1.0));
    // Soft fade on held weapon (GoldSrc viewmodels sit in depth range 0.0..0.3)
    float vmMask = mix(0.22, 1.0, smoothstep(0.12, 0.42, d));
    float skyFade = 1.0 - smoothstep(0.9955, 0.9988, d);

    float beam = cookie * distAtten * nDotL * vmMask * skyFade * uIntensity;

    // Slight warm-center / cool-rim halogen tint
    vec3 beamTint = mix(vec3(0.84, 0.91, 1.02), vec3(1.00, 0.97, 0.91), clamp(midBeam * 0.6 + hotCore * 0.4, 0.0, 1.0));

    // Gamma-lifted albedo illumination reveals real texture detail in dark corridors
    vec3 sceneCol = texture2D(uColor, vUv).rgb;
    vec3 liftedAlbedo = pow(max(sceneCol, vec3(0.0025)), vec3(0.58));
    vec3 lit = mix(sceneCol * 2.1, liftedAlbedo * 0.92, 0.62) * beamTint * beam * 1.35;

    gl_FragColor = vec4(lit, 1.0);
}
)GLSL";

        GlFlashApi::GLhandle vs = api_.glCreateShader(GlFlashApi::kVertexShader);
        GlFlashApi::GLhandle fs = api_.glCreateShader(GlFlashApi::kFragmentShader);
        api_.glShaderSource(vs, 1, &kVertSrc, nullptr);
        api_.glShaderSource(fs, 1, &kFragSrc, nullptr);
        api_.glCompileShader(vs);
        api_.glCompileShader(fs);

        int vsOk = 0, fsOk = 0;
        api_.glGetShaderiv(vs, GlFlashApi::kCompileStatus, &vsOk);
        api_.glGetShaderiv(fs, GlFlashApi::kCompileStatus, &fsOk);
        if (!vsOk || !fsOk) {
            if (api_.glGetShaderInfoLog) {
                char logBuf[512]{};
                int len = 0;
                api_.glGetShaderInfoLog(!vsOk ? vs : fs, sizeof(logBuf) - 1, &len, logBuf);
                LogMsg("[gl-flash] Shader compile error: %s", logBuf);
            }
            api_.glDeleteShader(vs);
            api_.glDeleteShader(fs);
            return false;
        }

        prog_ = api_.glCreateProgram();
        api_.glAttachShader(prog_, vs);
        api_.glAttachShader(prog_, fs);
        api_.glLinkProgram(prog_);
        api_.glDeleteShader(vs);
        api_.glDeleteShader(fs);

        int linkOk = 0;
        api_.glGetProgramiv(prog_, GlFlashApi::kLinkStatus, &linkOk);
        if (!linkOk) {
            if (api_.glGetProgramInfoLog) {
                char logBuf[512]{};
                int len = 0;
                api_.glGetProgramInfoLog(prog_, sizeof(logBuf) - 1, &len, logBuf);
                LogMsg("[gl-flash] Program link error: %s", logBuf);
            }
            api_.glDeleteProgram(prog_);
            prog_ = 0;
            return false;
        }

        uColor_     = api_.glGetUniformLocation(prog_, "uColor");
        uDepth_     = api_.glGetUniformLocation(prog_, "uDepth");
        uInvRes_    = api_.glGetUniformLocation(prog_, "uInvRes");
        uAspect_    = api_.glGetUniformLocation(prog_, "uAspect");
        uSway_      = api_.glGetUniformLocation(prog_, "uSway");
        uIntensity_ = api_.glGetUniformLocation(prog_, "uIntensity");
        return true;
    }

    GlFlashApi api_{};
    HGLRC ctx_ = nullptr;
    GlFlashApi::GLhandle prog_ = 0;
    unsigned int colorTex_ = 0;
    unsigned int depthTex_ = 0;
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
