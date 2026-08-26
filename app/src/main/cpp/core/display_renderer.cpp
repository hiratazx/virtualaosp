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

constexpr const char* kVertexSrc =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "    vUV = vec2(aUV.x, 1.0 - aUV.y);\n" /* guest frames are top-down */
    "}\n";

constexpr const char* kFragmentSrc =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uTex;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(uTex, vUV);\n"
    "}\n";

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

    static const GLfloat quad[] = {
        -1.f, -1.f, 0.f, 1.f,
         1.f, -1.f, 1.f, 1.f,
        -1.f,  1.f, 0.f, 0.f,
         1.f,  1.f, 1.f, 0.f,
    };

    glGenVertexArrays(1, &mVao); /* GLES3: glGenVertexArrays */
    glGenBuffers(1, &mVbo);
    glBindVertexArray(mVao);
    glBindBuffer(GL_ARRAY_BUFFER, mVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    GLint aPos = glGetAttribLocation(mProgram, "aPos");
    GLint aUV = glGetAttribLocation(mProgram, "aUV");
    glEnableVertexAttribArray(static_cast<GLuint>(aPos));
    glVertexAttribPointer(static_cast<GLuint>(aPos), 2, GL_FLOAT, GL_FALSE, 16,
                          reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(static_cast<GLuint>(aUV));
    glVertexAttribPointer(static_cast<GLuint>(aUV), 2, GL_FLOAT, GL_FALSE, 16,
                          reinterpret_cast<const void*>(8));
    glBindVertexArray(0);

    glGenTextures(1, &mTextureId);
    glBindTexture(GL_TEXTURE_2D, mTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    LOGI("GL pipeline ready");
}

void DisplayRenderer::renderLoop() {
    bool eglReady = false;

    while (mRunning) {
        {
            std::unique_lock<std::mutex> lock(mWindowMutex);
            /* Wait until the window is available AND any previous EGL context
             * has been fully torn down, so we never init on a stale display. */
            mCondition.wait(lock, [this]() {
                return !mRunning ||
                       (mWindow != nullptr && mEglDisplay == EGL_NO_DISPLAY);
            });

            if (!mRunning) break;

            if (!eglReady && mWindow != nullptr) {
                if (initEGL()) {
                    setupGL();
                    eglReady = true;
                    LOGI("EGL successfully initialized on viewport thread");
                } else {
                    terminateEGL();
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
            }
        }

        if (eglReady) {
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

            /* Auto-fit letterboxing: preserve the guest aspect ratio
             * inside the host window (VMware/VirtualBox style). */
            int vpW = winW, vpH = winH, vpX = 0, vpY = 0;
            if (gw > 0 && gh > 0 && winW > 0 && winH > 0) {
                const double guestAR = static_cast<double>(gw) / gh;
                const double hostAR = static_cast<double>(winW) / winH;
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

            bool hasData = false;
            {
                std::unique_lock<std::mutex> lock(mFrameMutex);
                if (mHasNewFrame && !mFrameBuffer.empty()) {
                    glBindTexture(GL_TEXTURE_2D, mTextureId);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mGuestFrameWidth,
                                 mGuestFrameHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                                 mFrameBuffer.data());
                    mHasNewFrame = false;
                    hasData = true;
                } else if (!mFrameBuffer.empty()) {
                    hasData = true;
                }
            }

            if (hasData) {
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                glUseProgram(mProgram);
                glBindVertexArray(mVao);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mTextureId);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            } else {
                /* Animated standby: accumulating phase drives three colour
                 * channels at staggered offsets giving a blue-slate shimmer.
                 * Distinct enough from a dead black screen at any brightness. */
                static float phase = 0.0f;
                phase += 0.03f;
                float r = 0.08f + 0.04f * std::sin(phase);
                float g = 0.10f + 0.05f * std::sin(phase + 1.5f);
                float b = 0.16f + 0.06f * std::sin(phase + 3.0f);
                glClearColor(r, g, b, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
            }

            if (!eglSwapBuffers(mEglDisplay, mEglSurface)) {
                EGLint err = eglGetError();
                LOGE("eglSwapBuffers failed with error 0x%x", err);
                if (err == EGL_BAD_SURFACE ||
                    err == EGL_BAD_DISPLAY ||
                    err == EGL_BAD_NATIVE_WINDOW) {
                    std::unique_lock<std::mutex> lock(mWindowMutex);
                    terminateEGL();
                    eglReady = false;
                }
            }

            if (!hasData) {
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            }
        }
    }

    if (eglReady) {
        std::unique_lock<std::mutex> lock(mWindowMutex);
        terminateEGL();
    }
}

} // namespace accore
