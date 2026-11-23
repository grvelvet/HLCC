#include "render/flashlight_gl.hpp"

#include <algorithm>
#include <cmath>

#include "core/log.hpp"

namespace gl_flash {

bool GlFlashApi::EnsureLoaded(const glfont::GlApi& gl) {
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
    auto gpExt = [&](const char* a, const char* b) -> void* {
        void* p = gp(a);
        return p ? p : gp(b);
    };
    glGenFramebuffers        = reinterpret_cast<decltype(glGenFramebuffers)>(gpExt("glGenFramebuffers", "glGenFramebuffersEXT"));
    glDeleteFramebuffers     = reinterpret_cast<decltype(glDeleteFramebuffers)>(gpExt("glDeleteFramebuffers", "glDeleteFramebuffersEXT"));
    glBindFramebuffer        = reinterpret_cast<decltype(glBindFramebuffer)>(gpExt("glBindFramebuffer", "glBindFramebufferEXT"));
    glFramebufferTexture2D   = reinterpret_cast<decltype(glFramebufferTexture2D)>(gpExt("glFramebufferTexture2D", "glFramebufferTexture2DEXT"));
    glCheckFramebufferStatus = reinterpret_cast<decltype(glCheckFramebufferStatus)>(gpExt("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT"));
    glBlitFramebuffer        = reinterpret_cast<decltype(glBlitFramebuffer)>(gpExt("glBlitFramebuffer", "glBlitFramebufferEXT"));

    ok = (glCreateShader && glShaderSource && glCompileShader && glGetShaderiv &&
          glCreateProgram && glAttachShader && glLinkProgram && glGetProgramiv &&
          glUseProgram && glGetUniformLocation && glUniform1i && glUniform1f &&
          glUniform2f && glCopyTexSubImage2D && glActiveTexture);
    if (!ok) {
        core::LogMsg("[gl-flash] Missing GL procs: cs=%d cp=%d copy=%d act=%d",
                     static_cast<int>(glCreateShader != nullptr),
                     static_cast<int>(glCreateProgram != nullptr),
                     static_cast<int>(glCopyTexSubImage2D != nullptr),
                     static_cast<int>(glActiveTexture != nullptr));
    }
    return ok;
}

void GlFlashlight::UpdateSway(float pitchDeg, float yawDeg, float dtSec) {
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

    swayX_ = std::clamp(swayX_ + dYaw * 0.0022f, -0.055f, 0.055f);
    swayY_ = std::clamp(swayY_ + dPitch * 0.0022f, -0.045f, 0.045f);
    float decay = std::exp(-std::clamp(dtSec, 0.001f, 0.1f) * 11.0f);
    swayX_ *= decay;
    swayY_ *= decay;
}

void GlFlashlight::ResetSway() noexcept {
    hasPrevAngles_ = false;
    swayX_ = 0.0f;
    swayY_ = 0.0f;
}

void GlFlashlight::Render(const glfont::GlApi& gl, HGLRC ctx, int w, int h, float intensityScale) {
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

    int curFb = 0;
    if (api_.glGetIntegerv) {
        api_.glGetIntegerv(GlFlashApi::kFramebufferBind, &curFb);
    }
    bool usedBlit = false;
    if (curFb != 0 && resolveFbo_ != 0 && api_.glBlitFramebuffer && api_.glBindFramebuffer) {
        usedBlit = true;
        if (api_.glGetError) while (api_.glGetError() != 0) {}
        api_.glBindFramebuffer(GlFlashApi::kReadFramebuffer, static_cast<unsigned int>(curFb));
        api_.glBindFramebuffer(GlFlashApi::kDrawFramebuffer, resolveFbo_);
        api_.glBlitFramebuffer(0, 0, w, h, 0, 0, w, h,
                               GlFlashApi::kColorBufferBit | GlFlashApi::kDepthBufferBit,
                               GlFlashApi::kNearest);
        unsigned int blitErr = api_.glGetError ? api_.glGetError() : 0;
        if (blitErr != 0 && !useDepthStencil_) {
            api_.glBindFramebuffer(GlFlashApi::kFramebuffer, static_cast<unsigned int>(curFb));
            useDepthStencil_ = true;
            AttachResolveFbo(gl, w, h);
            api_.glBindFramebuffer(GlFlashApi::kReadFramebuffer, static_cast<unsigned int>(curFb));
            api_.glBindFramebuffer(GlFlashApi::kDrawFramebuffer, resolveFbo_);
            api_.glBlitFramebuffer(0, 0, w, h, 0, 0, w, h,
                                   GlFlashApi::kColorBufferBit | GlFlashApi::kDepthBufferBit,
                                   GlFlashApi::kNearest);
            blitErr = api_.glGetError ? api_.glGetError() : 0;
        }
        if (!loggedDepthOnce_ && api_.glReadPixels) {
            loggedDepthOnce_ = true;
            api_.glBindFramebuffer(GlFlashApi::kReadFramebuffer, resolveFbo_);
            float cd = -1.0f;
            api_.glReadPixels(w / 2, h / 2, 1, 1, GlFlashApi::kDepthComponent, 0x1406 /* GL_FLOAT */, &cd);
            core::LogMsg("[gl-flash] render diag (blit ds=%d): w=%d h=%d fb=%d prevProg=%d centerDepth=%.6f err=0x%X",
                         static_cast<int>(useDepthStencil_), w, h, curFb, prevProg, cd, blitErr);
        }
        api_.glBindFramebuffer(GlFlashApi::kFramebuffer, static_cast<unsigned int>(curFb));

        api_.glActiveTexture(glfont::GlApi::kTexture0);
        gl.glEnable(glfont::GlApi::kTexture2D);
        gl.glBindTexture(glfont::GlApi::kTexture2D, colorTex_);

        api_.glActiveTexture(glfont::GlApi::kTexture0 + 1);
        gl.glEnable(glfont::GlApi::kTexture2D);
        gl.glBindTexture(glfont::GlApi::kTexture2D, depthTex_);
    }
    if (!usedBlit) {
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
            core::LogMsg("[gl-flash] render diag: w=%d h=%d fb=%d prevProg=%d centerDepth=%.6f err=0x%X",
                         w, h, curFb, prevProg, cd, api_.glGetError());
        }
    }

    gl.glMatrixMode(glfont::GlApi::kProjection);
    gl.glPushMatrix();
    gl.glLoadIdentity();
    gl.glOrtho(0.0, static_cast<double>(w), 0.0, static_cast<double>(h), -1.0, 1.0);
    gl.glMatrixMode(glfont::GlApi::kModelview);
    gl.glPushMatrix();
    gl.glLoadIdentity();

    gl.glEnable(glfont::GlApi::kBlend);
    gl.glBlendFunc(GlFlashApi::kOne, GlFlashApi::kOne);

    api_.glUseProgram(prog_);
    api_.glUniform1i(uColor_, 0);
    api_.glUniform1i(uDepth_, 1);
    api_.glUniform2f(uInvRes_, 1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h));
    api_.glUniform1f(uAspect_, static_cast<float>(w) / static_cast<float>(h));
    api_.glUniform2f(uSway_, swayX_, swayY_);
    api_.glUniform1f(uIntensity_, std::clamp(intensityScale, 0.2f, 2.5f));

    gl.glColor4ub(255, 255, 255, 255);
    gl.glBegin(glfont::GlApi::kQuads);
    gl.glTexCoord2f(0.0f, 0.0f); gl.glVertex2f(0.0f, 0.0f);
    gl.glTexCoord2f(1.0f, 0.0f); gl.glVertex2f(static_cast<float>(w), 0.0f);
    gl.glTexCoord2f(1.0f, 1.0f); gl.glVertex2f(static_cast<float>(w), static_cast<float>(h));
    gl.glTexCoord2f(0.0f, 1.0f); gl.glVertex2f(0.0f, static_cast<float>(h));
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

void GlFlashlight::AttachResolveFbo(const glfont::GlApi& gl, int w, int h) {
    gl.glBindTexture(glfont::GlApi::kTexture2D, depthTex_);
    gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexMinFilter, GlFlashApi::kNearest);
    gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexMagFilter, GlFlashApi::kNearest);
    gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexWrapS, GlFlashApi::kClampToEdge);
    gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexWrapT, GlFlashApi::kClampToEdge);
    if (useDepthStencil_) {
        gl.glTexImage2D(glfont::GlApi::kTexture2D, 0, GlFlashApi::kDepth24Stencil8, w, h, 0,
                        GlFlashApi::kDepthStencil, GlFlashApi::kUnsignedInt248, nullptr);
    } else {
        gl.glTexImage2D(glfont::GlApi::kTexture2D, 0, GlFlashApi::kDepthComponent24, w, h, 0,
                        GlFlashApi::kDepthComponent, GlFlashApi::kUnsignedInt, nullptr);
    }
    gl.glBindTexture(glfont::GlApi::kTexture2D, 0);

    if (api_.glGenFramebuffers && api_.glBindFramebuffer && api_.glFramebufferTexture2D) {
        int prev = 0;
        if (api_.glGetIntegerv) api_.glGetIntegerv(GlFlashApi::kFramebufferBind, &prev);
        if (!resolveFbo_) api_.glGenFramebuffers(1, &resolveFbo_);
        api_.glBindFramebuffer(GlFlashApi::kFramebuffer, resolveFbo_);
        api_.glFramebufferTexture2D(GlFlashApi::kFramebuffer, GlFlashApi::kColorAttachment0,
                                    glfont::GlApi::kTexture2D, colorTex_, 0);
        api_.glFramebufferTexture2D(GlFlashApi::kFramebuffer,
                                    useDepthStencil_ ? GlFlashApi::kDepthStencilAtt : GlFlashApi::kDepthAttachment,
                                    glfont::GlApi::kTexture2D, depthTex_, 0);
        if (api_.glCheckFramebufferStatus &&
            api_.glCheckFramebufferStatus(GlFlashApi::kFramebuffer) != GlFlashApi::kFboComplete) {
            if (api_.glDeleteFramebuffers) api_.glDeleteFramebuffers(1, &resolveFbo_);
            resolveFbo_ = 0;
        }
        api_.glBindFramebuffer(GlFlashApi::kFramebuffer, static_cast<unsigned int>(prev));
    }
}

bool GlFlashlight::EnsureResources(const glfont::GlApi& gl, HGLRC ctx, int w, int h) {
    if (ctx != ctx_) {
        ctx_ = ctx;
        prog_ = 0;
        colorTex_ = 0;
        depthTex_ = 0;
        resolveFbo_ = 0;
        useDepthStencil_ = false;
        texW_ = 0;
        texH_ = 0;
        shaderFailed_ = false;
        loggedDepthOnce_ = false;
    }
    if (shaderFailed_) return false;

    if (prog_ == 0) {
        if (!CompileShaderProgram()) {
            shaderFailed_ = true;
            return false;
        }
        core::LogMsg("[gl-flash] Shader program compiled successfully");
    }
    if (texW_ != w || texH_ != h || colorTex_ == 0 || depthTex_ == 0) {
        if (colorTex_) gl.glDeleteTextures(1, &colorTex_);
        if (depthTex_) gl.glDeleteTextures(1, &depthTex_);
        if (resolveFbo_ && api_.glDeleteFramebuffers) {
            api_.glDeleteFramebuffers(1, &resolveFbo_);
            resolveFbo_ = 0;
        }
        gl.glGenTextures(1, &colorTex_);
        gl.glGenTextures(1, &depthTex_);

        gl.glBindTexture(glfont::GlApi::kTexture2D, colorTex_);
        gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexMinFilter, GlFlashApi::kNearest);
        gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexMagFilter, GlFlashApi::kNearest);
        gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexWrapS, GlFlashApi::kClampToEdge);
        gl.glTexParameteri(glfont::GlApi::kTexture2D, glfont::GlApi::kTexWrapT, GlFlashApi::kClampToEdge);
        gl.glTexImage2D(glfont::GlApi::kTexture2D, 0, glfont::GlApi::kRgba, w, h, 0,
                        glfont::GlApi::kRgba, glfont::GlApi::kUnsignedByte, nullptr);
        gl.glBindTexture(glfont::GlApi::kTexture2D, 0);

        AttachResolveFbo(gl, w, h);
        texW_ = w;
        texH_ = h;
    }
    return true;
}

bool GlFlashlight::CompileShaderProgram() {
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
    if (d >= 0.9988 || d <= 0.0001) {
        gl_FragColor = vec4(0.0);
        return;
    }

    vec3 pos = ViewPosFromDepth(vUv, d);

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

    vec3 lightOrigin = vec3(6.5, -5.0, 0.0);
    vec3 lightDir = normalize(vec3(-0.014 + uSway.x, 0.010 + uSway.y, 1.0));
    vec3 toFrag = pos - lightOrigin;
    float dist = length(toFrag);
    vec3 dir = toFrag / max(dist, 0.001);

    float cosA = dot(dir, lightDir);
    float outerPenumbra = smoothstep(0.922, 0.962, cosA);
    float midBeam       = smoothstep(0.955, 0.982, cosA);
    float hotCore       = smoothstep(0.980, 0.996, cosA);
    float lensRing      = exp(-pow((cosA - 0.956) * 52.0, 2.0)) * 0.24 * outerPenumbra;
    float cookie        = outerPenumbra * 0.55 + midBeam * 0.45 + hotCore * 0.50 + lensRing;

    float invSq = 1.0 / (1.0 + 0.0013 * dist + 0.0000030 * dist * dist);
    float rangeFade = smoothstep(1150.0, 200.0, dist);
    float distAtten = invSq * rangeFade;

    float nDotL = mix(0.38, 1.0, clamp(abs(dot(normal, -dir)), 0.0, 1.0));
    float vmMask = mix(0.22, 1.0, smoothstep(0.12, 0.42, d));
    float skyFade = 1.0 - smoothstep(0.9955, 0.9988, d);

    float beam = cookie * distAtten * nDotL * vmMask * skyFade * uIntensity;
    vec3 beamTint = mix(vec3(0.84, 0.91, 1.02), vec3(1.00, 0.97, 0.91), clamp(midBeam * 0.6 + hotCore * 0.4, 0.0, 1.0));

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
            core::LogMsg("[gl-flash] Shader compile error: %s", logBuf);
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
            core::LogMsg("[gl-flash] Program link error: %s", logBuf);
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

} // namespace gl_flash
