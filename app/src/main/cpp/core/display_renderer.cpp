#include "display_renderer.h"
#include "frame_channel.h"
#include "log.h"

#include <chrono>
#include <cmath>
#include <cstring>

#define TAG "ContainerRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace accore {

namespace {
std::atomic<bool> g_diagnostic_fallback{true};
} // namespace

void setDiagnosticFallbackEnabled(bool enabled) {
    g_diagnostic_fallback.store(enabled, std::memory_order_relaxed);
}

bool diagnosticFallbackEnabled() {
    return g_diagnostic_fallback.load(std::memory_order_relaxed);
}

DisplayRenderer& DisplayRenderer::getInstance() {
    static DisplayRenderer instance;
    return instance;
}

DisplayRenderer::DisplayRenderer() = default;

DisplayRenderer::~DisplayRenderer() {
    destroyWindow();
    mRunning = false;
    mCondition.notify_all();
    if (mRenderThread.joinable()) mRenderThread.join();
    if (mPumpThread.joinable()) mPumpThread.join();
}

void DisplayRenderer::setNativeWindow(ANativeWindow* window) {
    ensurePumpStarted();
    {
        std::unique_lock<std::mutex> lock(mWindowMutex);
        if (window == mWindow) return;

        /* The renderer owns a reference for as long as it renders. */
        if (window != nullptr) ANativeWindow_acquire(window);
        if (mWindow != nullptr) ANativeWindow_release(mWindow);
        mWindow = window;
        if (window != nullptr) {
            mWidth = ANativeWindow_getWidth(window);
            mHeight = ANativeWindow_getHeight(window);
        }
        LOGI("native window bound: %dx%d", mWidth, mHeight);
    }
    mCondition.notify_one();

    if (!mRunning.exchange(true)) {
        mRenderThread = std::thread(&DisplayRenderer::renderLoop, this);
    }
}

void DisplayRenderer::updateWindowSize(int width, int height) {
    std::unique_lock<std::mutex> lock(mWindowMutex);
    if (mWidth != width || mHeight != height) {
        mWidth = width;
        mHeight = height;
        mSizeChanged = true;
        LOGI("Host viewport resized: %dx%d", width, height);
        mCondition.notify_one();
    }
}

void DisplayRenderer::destroyWindow() {
    std::unique_lock<std::mutex> lock(mWindowMutex);
    /* Terminate EGL immediately so the BufferQueue is released before
     * the Surface object is invalidated on the Kotlin side. This prevents
     * the "Abandoned BufferQueue" spam in eglSwapBuffers. */
    terminateEGL();
    if (mWindow != nullptr) {
        ANativeWindow_release(mWindow);
        mWindow = nullptr;
    }
    mCondition.notify_all();
}

void DisplayRenderer::ensurePumpStarted() {
    if (!mPumpThread.joinable()) {
        mPumpThread = std::thread(&DisplayRenderer::pumpLoop, this);
    }
}

void DisplayRenderer::updateGuestFrame(const uint8_t* rgbaBuffer, int width, int height) {
    if (!rgbaBuffer || width <= 0 || height <= 0) return;
    {
        std::unique_lock<std::mutex> lock(mFrameMutex);
        mGuestFrameWidth  = width;
        mGuestFrameHeight = height;
        const size_t bufferSize = static_cast<size_t>(width * height * 4);
        if (mFrameBuffer.size() != bufferSize) {
            mFrameBuffer.resize(bufferSize);
        }
        std::memcpy(mFrameBuffer.data(), rgbaBuffer, bufferSize);
        mHasNewFrame = true;
    }
    mCondition.notify_one();
}


/* Bridge between the memfd seqlock channel (guest transport, unchanged)
 * and the EGL renderer's texture upload path. */
void DisplayRenderer::pumpLoop() {
    thread_local std::vector<uint8_t> frame;

    while (mRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));

        FrameChannelHost* channel = HostChannel();
        if (channel == nullptr || channel->header() == nullptr) continue;

        const size_t need = static_cast<size_t>(channel->header()->width) *
                            channel->header()->height * 4;
        frame.resize(need);

        FrameChannelHost::Snapshot snap;
        if (channel->ReadLatest(frame.data(), frame.size(), &snap)) {
            updateGuestFrame(frame.data(),
                             static_cast<int>(snap.width),
                             static_cast<int>(snap.height));
        }
    }
}

bool DisplayRenderer::initEGL() {
    mEglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (mEglDisplay == EGL_NO_DISPLAY) return false;

    EGLint major = 0, minor = 0;
    if (!eglInitialize(mEglDisplay, &major, &minor)) {
        LOGE("eglInitialize failed");
        return false;
    }

    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(mEglDisplay, configAttribs, &config, 1, &numConfigs) ||
        numConfigs < 1) {
        LOGE("eglChooseConfig failed");
        return false;
    }

    const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    mEglContext = eglCreateContext(mEglDisplay, config, EGL_NO_CONTEXT, contextAttribs);
    if (mEglContext == EGL_NO_CONTEXT) {
        const EGLint es2Attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        mEglContext = eglCreateContext(mEglDisplay, config, EGL_NO_CONTEXT, es2Attribs);
    }
    if (mEglContext == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed");
        return false;
    }

    mEglSurface = eglCreateWindowSurface(mEglDisplay, config, mWindow, nullptr);
    if (mEglSurface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed 0x%x", eglGetError());
        return false;
    }

    if (!eglMakeCurrent(mEglDisplay, mEglSurface, mEglSurface, mEglContext)) {
        LOGE("eglMakeCurrent failed 0x%x", eglGetError());
        return false;
    }

    LOGI("EGL %d.%d ready", major, minor);
    return true;
}

void DisplayRenderer::terminateEGL() {
    if (mEglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (mEglSurface != EGL_NO_SURFACE) {
            eglDestroySurface(mEglDisplay, mEglSurface);
            mEglSurface = EGL_NO_SURFACE;
        }
        if (mEglContext != EGL_NO_CONTEXT) {
            eglDestroyContext(mEglDisplay, mEglContext);
            mEglContext = EGL_NO_CONTEXT;
        }
        eglTerminate(mEglDisplay);
        mEglDisplay = EGL_NO_DISPLAY;
    }
}

namespace {

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LOGE("shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

/* ES 3.00 vertex shader — layout(location) replaces glGetAttribLocation. */
constexpr const char* kVertexSrc = R"(#version 300 es
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y); /* guest frames are top-down */
}
)";

/* ES 3.00 fragment shader — animated diagnostic canvas while awaiting guest
 * compositor; textured quad when a guest frame is available. */
constexpr const char* kFragmentSrc = R"(#version 300 es
precision mediump float;
in  vec2 vTexCoord;
out vec4 fragColor;
uniform sampler2D uTexture;
uniform int   uUseTexture;
uniform float uTime;
void main() {
    if (uUseTexture == 1) {
        fragColor = texture(uTexture, vTexCoord);
    } else {
        vec2 uv = vTexCoord;
        float r = 0.15 + 0.10 * sin(uv.x * 10.0 + uTime * 2.0);
        float g = 0.20 + 0.15 * sin(uv.y * 10.0 + uTime * 3.0);
        float b = 0.35 + 0.20 * cos((uv.x + uv.y) * 8.0 + uTime);
        /* Subtle grid overlay — confirms the container engine is alive. */
        if (fract(uv.x * 20.0) < 0.03 || fract(uv.y * 20.0) < 0.03) {
            fragColor = vec4(0.4, 0.6, 0.9, 1.0);
        } else {
            fragColor = vec4(r, g, b, 1.0);
        }
    }
}
)";

} // namespace

void DisplayRenderer::setupGL() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentSrc);
    mProgram = glCreateProgram();
    glAttachShader(mProgram, vs);
    glAttachShader(mProgram, fs);
    glLinkProgram(mProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = GL_FALSE;
    glGetProgramiv(mProgram, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        char log[512];
        glGetProgramInfoLog(mProgram, sizeof(log), nullptr, log);
        LOGE("program link failed: %s", log);
        return;
    }

    /* Full-screen quad: XY position + UV coords, stride = 4 floats. */
    static const GLfloat quad[] = {
        -1.f, -1.f,  0.f, 1.f,
         1.f, -1.f,  1.f, 1.f,
        -1.f,  1.f,  0.f, 0.f,
         1.f,  1.f,  1.f, 0.f,
    };

    glGenVertexArrays(1, &mVao);
    glGenBuffers(1, &mVbo);
    glBindVertexArray(mVao);
    glBindBuffer(GL_ARRAY_BUFFER, mVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    /* location = 0: vec2 aPosition */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void*>(0));
    /* location = 1: vec2 aTexCoord */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void*>(2 * sizeof(float)));
    glBindVertexArray(0);

    glGenTextures(1, &mTextureId);
    glBindTexture(GL_TEXTURE_2D, mTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    LOGI("GL pipeline ready (ES3, layout(location) attributes)");
}

void DisplayRenderer::renderLoop() {
    bool eglReady = false;
    float timeVal = 0.0f;
    GLint useTextureLoc = -1;
    GLint timeLoc = -1;

    while (mRunning) {
        /* ---- Phase 1: wait for a valid window ---- */
        {
            std::unique_lock<std::mutex> lock(mWindowMutex);
            if (mWindow == nullptr || mEglDisplay != EGL_NO_DISPLAY) {
                /* Either no window yet, or EGL already alive — don't re-init.
                 * Poll at 50 ms so we respond quickly when the surface arrives
                 * without burning CPU while idle. */
                if (mWindow == nullptr) {
                    mCondition.wait_for(lock, std::chrono::milliseconds(50));
                    continue;
                }
                /* Window present + EGL already up: fall through to render. */
            }

            /* ---- Phase 2: init EGL (first time or after surface loss) ---- */
            if (!eglReady && mWindow != nullptr) {
                if (initEGL()) {
                    setupGL();
                    /* Cache uniform locations after link so we don't call
                     * glGetUniformLocation every frame. */
                    useTextureLoc = glGetUniformLocation(mProgram, "uUseTexture");
                    timeLoc       = glGetUniformLocation(mProgram, "uTime");
                    eglReady = true;
                    LOGI("EGL and shaders initialised — entering 60 fps render loop");
                } else {
                    terminateEGL();
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
            }
        }

        if (!eglReady) continue;

        /* ---- Phase 3: render one frame ---- */

        /* Letterbox viewport. */
        int winW, winH, gw, gh;
        {
            std::unique_lock<std::mutex> lock(mWindowMutex);
            winW = mWidth;
            winH = mHeight;
        }
        {
            std::unique_lock<std::mutex> lock(mFrameMutex);
            gw = mGuestFrameWidth;
            gh = mGuestFrameHeight;
        }

        int vpW = winW, vpH = winH, vpX = 0, vpY = 0;
        if (gw > 0 && gh > 0 && winW > 0 && winH > 0) {
            const double guestAR = static_cast<double>(gw) / gh;
            const double hostAR  = static_cast<double>(winW) / winH;
            if (guestAR > hostAR) {
                vpW = winW;
                vpH = static_cast<int>(winW / guestAR);
            } else {
                vpH = winH;
                vpW = static_cast<int>(winH * guestAR);
            }
            vpX = (winW - vpW) / 2;
            vpY = (winH - vpH) / 2;
        }
        glViewport(vpX, vpY, vpW, vpH);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(mProgram);
        glBindVertexArray(mVao);

        /* Upload the latest guest frame if one arrived since last tick. */
        bool hasGuestTexture = false;
        {
            std::unique_lock<std::mutex> lock(mFrameMutex);
            if (mHasNewFrame && !mFrameBuffer.empty()) {
                glBindTexture(GL_TEXTURE_2D, mTextureId);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                             mGuestFrameWidth, mGuestFrameHeight,
                             0, GL_RGBA, GL_UNSIGNED_BYTE, mFrameBuffer.data());
                mHasNewFrame = false;
                hasGuestTexture = true;
            } else if (!mFrameBuffer.empty()) {
                hasGuestTexture = true;
            }
        }

        timeVal += 0.016f;
        if (timeLoc       >= 0) glUniform1f(timeLoc,       timeVal);
        if (useTextureLoc >= 0) glUniform1i(useTextureLoc, hasGuestTexture ? 1 : 0);

        if (hasGuestTexture) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, mTextureId);
        }
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        if (!eglSwapBuffers(mEglDisplay, mEglSurface)) {
            EGLint err = eglGetError();
            LOGE("eglSwapBuffers failed 0x%x", err);
            if (err == EGL_BAD_SURFACE ||
                err == EGL_BAD_DISPLAY ||
                err == EGL_BAD_NATIVE_WINDOW) {
                std::unique_lock<std::mutex> lock(mWindowMutex);
                terminateEGL();
                eglReady = false;
            }
        }

        /* Unconditional 16 ms pace — ~60 fps whether or not a guest frame
         * arrived.  This keeps CPU load predictable and prevents the render
         * thread from busy-spinning when the guest compositor is fast. */
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    if (eglReady) {
        std::unique_lock<std::mutex> lock(mWindowMutex);
        terminateEGL();
    }
}

} // namespace accore
