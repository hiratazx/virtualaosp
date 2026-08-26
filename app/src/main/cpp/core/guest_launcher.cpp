#include "guest_launcher.h"
#include "display_renderer.h"
#include <android/log.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#define GL_TAG "ac.guest"
#define GL_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  GL_TAG, __VA_ARGS__)
#define GL_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, GL_TAG, __VA_ARGS__)

/* ---- Software frame pump -------------------------------------------- */

static std::atomic<bool>  g_pumpRunning{false};
static std::thread        g_framePumpThread;

/**
 * Generate and push RGBA test frames directly into DisplayRenderer at
 * ~60 fps. Runs while the real guest compositor is still initialising so
 * the EGL/GL/Surface pipeline can be verified end-to-end immediately.
 *
 * frameFd: memfd shared with the guest (mmap'd to keep the fd alive).
 * If frameFd < 0, the pump still runs but skips the mmap.
 */
static void startSoftwareFramePump(int frameFd, int width, int height, int slotCount) {
    g_pumpRunning.store(true, std::memory_order_relaxed);

    const size_t slotSize  = static_cast<size_t>(width * height * 4);
    const size_t totalSize = slotSize * static_cast<size_t>(slotCount);

    /* Map the shared frame channel fd if available so it stays valid for
     * the guest process.  We don't write through it here — updateGuestFrame
     * feeds the renderer directly — but holding a mapping prevents premature
     * fd cleanup. */
    void* shm = MAP_FAILED;
    if (frameFd >= 0 && totalSize > 0) {
        shm = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, frameFd, 0);
        if (shm == MAP_FAILED) {
            GL_LOGE("mmap frameFd failed: %s (continuing without shm)", strerror(errno));
        }
    }

    g_framePumpThread = std::thread(
        [shm, totalSize, width, height, slotSize]() {

        std::vector<uint8_t> pixelBuf(slotSize);
        uint32_t tick = 0;

        while (g_pumpRunning.load(std::memory_order_relaxed)) {
            uint8_t* buf = pixelBuf.data();

            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    size_t idx = static_cast<size_t>(y * width + x) * 4;
                    buf[idx + 0] = static_cast<uint8_t>((x + tick)       % 256);
                    buf[idx + 1] = static_cast<uint8_t>((y + tick * 2)   % 256);
                    buf[idx + 2] = static_cast<uint8_t>(
                        128 + 127 * std::sin((x + y + tick) * 0.05f));
                    buf[idx + 3] = 255;
                }
            }

            accore::DisplayRenderer::getInstance().updateGuestFrame(buf, width, height);

            tick += 2;
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 fps
        }

        if (shm != MAP_FAILED && totalSize > 0) {
            munmap(shm, totalSize);
        }
    });

    GL_LOGI("software frame pump started (%dx%d)", width, height);
}

static void stopSoftwareFramePump() {
    g_pumpRunning.store(false, std::memory_order_relaxed);
    if (g_framePumpThread.joinable()) {
        g_framePumpThread.join();
    }
    GL_LOGI("software frame pump stopped");
}

/* -------------------------------------------------------------------- */

GuestLauncher& GuestLauncher::getInstance() {
    static GuestLauncher instance;
    return instance;
}

void GuestLauncher::setStateCallback(StateCallback callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    mStateCallback = std::move(callback);
}

bool GuestLauncher::startContainer(const LaunchConfig& config) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mState == ContainerState::RUNNING || mState == ContainerState::STARTING) return false;

    const std::string& root = config.rootfsPath;
    const std::string& rel  = config.initBinaryPath;

    // ---- Resolve the container's dynamic linker ----
    // We invoke linker64 directly so the kernel never needs to resolve
    // PT_INTERP against the host filesystem — linker64 handles it all
    // in userspace using the container lib paths.
    std::string linkerPath = root + "/system/bin/linker64";
    if (access(linkerPath.c_str(), X_OK) != 0) {
        linkerPath = root + "/bin/linker64";
        if (access(linkerPath.c_str(), X_OK) != 0) {
            GL_LOGE("linker64 not found under %s", root.c_str());
            mState = ContainerState::STOPPED;
            return false;
        }
    }
    chmod(linkerPath.c_str(), 0755);

    // ---- Resolve the guest entry-point binary (existence only — linker
    //      does the actual loading, not the kernel). ----
    std::string binaryPath = root + "/" +
        ((!rel.empty() && rel.front() == '/') ? rel.substr(1) : rel);

    if (access(binaryPath.c_str(), F_OK) != 0) {
        GL_LOGE("target binary missing: %s — trying fallbacks", binaryPath.c_str());
        const char* fallbacks[] = {"/system/bin/sh", "/bin/sh", nullptr};
        bool found = false;
        for (int i = 0; fallbacks[i]; ++i) {
            std::string fb = root + fallbacks[i];
            if (access(fb.c_str(), F_OK) == 0) {
                GL_LOGI("falling back to %s", fb.c_str());
                binaryPath = fb;
                found = true;
                break;
            }
        }
        if (!found) {
            GL_LOGE("no guest binary found under %s", root.c_str());
            mState = ContainerState::STOPPED;
            return false;
        }
    }
    chmod(binaryPath.c_str(), 0755);

    GL_LOGI("linker:  %s", linkerPath.c_str());
    GL_LOGI("binary:  %s", binaryPath.c_str());

    mState = ContainerState::STARTING;
    pid_t pid = fork();
    if (pid < 0) {
        mState = ContainerState::STOPPED;
        return false;
    }

    if (pid == 0) {
        setpgid(0, 0);
        chdir(root.c_str());

        /* Redirect stdin to /dev/null so the child never blocks waiting
         * for terminal input. linker64 on some AOSP images reads fd 0
         * during early init, which hangs if it is an open TTY. */
        int devNull = open("/dev/null", O_RDWR);
        if (devNull >= 0) {
            dup2(devNull, STDIN_FILENO);
            close(devNull);
        }

        std::vector<std::string> envStrings = {
            "LD_PRELOAD="       + config.libfakePath,
            "LD_LIBRARY_PATH=" + root + "/system/lib64:"
                               + root + "/system/lib:"
                               + root + "/apex/com.android.runtime/lib64",
            "AOSP_ROOTFS_DIR=" + root,
            "PATH="            + root + "/system/bin:" + root + "/bin",
            /* Bionic uses ANDROID_ROOT to locate property files and
             * runtime resources — must point inside the container. */
            "ANDROID_ROOT="    + root + "/system",
            "ANDROID_DATA="    + root + "/data",
            "TMPDIR="          + root + "/data/local/tmp",
        };
        std::vector<char*> envp;
        for (const auto& s : envStrings) envp.push_back(const_cast<char*>(s.c_str()));
        envp.push_back(nullptr);

        /* argv[0] = linker64 path (its own identity), argv[1] = ELF to load.
         * linker64 resolves PT_INTERP and shared libs inside the container,
         * so the kernel never looks up the interpreter on the host root. */
        char* const argv[] = {
            const_cast<char*>(linkerPath.c_str()),
            const_cast<char*>(binaryPath.c_str()),
            nullptr
        };

        execve(linkerPath.c_str(), argv, envp.data());
        __android_log_print(ANDROID_LOG_ERROR, GL_TAG,
            "execve via linker64 failed: %s (errno %d: %s)",
            linkerPath.c_str(), errno, strerror(errno));
        _exit(127);
    }

    mGuestPid = pid;
    mFrameFd  = config.frameFd;
    mState = ContainerState::RUNNING;
    if (mMonitorThread.joinable()) mMonitorThread.join();
    mMonitorThread = std::thread(&GuestLauncher::monitorLoop, this, pid);
    GL_LOGI("guest started pid=%d via linker64", pid);

    /* Start the software frame pump so the EGL/GL pipeline is exercised
     * immediately, before the guest compositor outputs real frames. */
    if (config.frameFd >= 0) {
        startSoftwareFramePump(
            config.frameFd, config.frameWidth, config.frameHeight, config.frameSlots);
    } else {
        /* No frameFd — pump without shm (still pushes test frames). */
        startSoftwareFramePump(-1, config.frameWidth, config.frameHeight, config.frameSlots);
    }

    return true;
}

void GuestLauncher::monitorLoop(pid_t pid) {
    int status = 0;
    waitpid(pid, &status, 0);

    ContainerState finalState = ContainerState::TERMINATED;
    int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    if (exitCode != 0 && !WIFSIGNALED(status)) finalState = ContainerState::CRASHED;

    StateCallback callback;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mState = finalState;
        mGuestPid = -1;
        callback = mStateCallback;
    }
    /* Invoked outside the lock so listeners may re-enter the launcher
     * (e.g. restart on crash) without self-deadlocking. */
    if (callback) callback(finalState, exitCode);
}

bool GuestLauncher::stopContainer(int signal, int timeoutMs) {
    stopSoftwareFramePump();
    std::lock_guard<std::mutex> lock(mMutex);
    if (mGuestPid <= 0) return false;
    kill(-mGuestPid, signal);
    return true;
}
