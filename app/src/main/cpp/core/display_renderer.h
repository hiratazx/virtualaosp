#pragma once

#include <android/native_window.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>

namespace accore {

class DisplayRenderer {
public:
    static DisplayRenderer& getInstance();

    void setNativeWindow(ANativeWindow* window);
    void updateWindowSize(int width, int height);
    void destroyWindow();
    void updateGuestFrame(const uint8_t* rgbaBuffer, int width, int height);

private:
    DisplayRenderer();
    ~DisplayRenderer();

    void renderLoop();
    bool initEGL();
    void terminateEGL();
    void setupGL();

    /* Pulls seqlock-stable frames from the shared memfd channel and
     * pushes them into updateGuestFrame (guest transport unchanged). */
    void ensurePumpStarted();
    void pumpLoop();

    ANativeWindow* mWindow{nullptr};
    std::mutex mWindowMutex;
    std::condition_variable mCondition;

    std::thread mRenderThread;
    std::thread mPumpThread;
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mHasNewFrame{false};
    std::atomic<bool> mSizeChanged{false};

    EGLDisplay mEglDisplay{EGL_NO_DISPLAY};
    EGLSurface mEglSurface{EGL_NO_SURFACE};
    EGLContext mEglContext{EGL_NO_CONTEXT};

    GLuint mProgram{0};
    GLuint mTextureId{0};
    GLuint mVao{0};
    GLuint mVbo{0};

    int mWidth{1080};
    int mHeight{2400};
    int mGuestFrameWidth{1080};
    int mGuestFrameHeight{2400};

    std::vector<uint8_t> mFrameBuffer;
    std::mutex mFrameMutex;
};

} // namespace accore
