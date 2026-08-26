#include "guest_launcher.h"
#include "display_renderer.h"
#include <android/log.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define GL_TAG "ac.guest"
#define GL_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  GL_TAG, __VA_ARGS__)
#define GL_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, GL_TAG, __VA_ARGS__)

/* ---- Software frame pump -------------------------------------------- */

static std::atomic<bool>  g_pumpRunning{false};
static std::thread        g_framePumpThread;
static std::thread        g_logPipeThread;

/* Resolve the first path in candidates that exists under rootfs.
 * Also chmods it to 0755 so execve never fails with EPERM. */
static std::string resolveRootfsPath(
        const std::string& root,
        std::initializer_list<const char*> candidates) {
    for (const char* rel : candidates) {
        std::string full = root + "/" +
            (*rel == '/' ? rel + 1 : rel);  /* strip leading slash */
        if (access(full.c_str(), F_OK) == 0) {
            chmod(full.c_str(), 0755);
            return full;
        }
    }
    return {};
}

/**
 * Push animated RGBA test frames directly into DisplayRenderer at ~60 fps.
 * Bypasses the shared-memory channel so the EGL/GL/Surface pipeline is
 * exercised immediately, before the guest compositor outputs real frames.
 */
static void startSoftwareFramePump(int width, int height) {
    if (g_pumpRunning.exchange(true)) return;  // already running

    g_framePumpThread = std::thread([width, height]() {
        GL_LOGI("software frame feeder started at %dx%d", width, height);
        const size_t bufSize = static_cast<size_t>(width * height * 4);
        std::vector<uint8_t> buffer(bufSize, 255);
        uint32_t frameCount = 0;

        while (g_pumpRunning.load(std::memory_order_relaxed)) {
            frameCount++;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    size_t idx = static_cast<size_t>(y * width + x) * 4;
                    buffer[idx + 0] = static_cast<uint8_t>((x + frameCount * 2) % 256);
                    buffer[idx + 1] = static_cast<uint8_t>((y + frameCount)     % 256);
                    buffer[idx + 2] = static_cast<uint8_t>((x + y + frameCount) % 256);
                    buffer[idx + 3] = 255;
                }
            }
            accore::DisplayRenderer::getInstance().updateGuestFrame(
                buffer.data(), width, height);
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 fps
        }
        GL_LOGI("software frame feeder stopped");
    });

    GL_LOGI("software frame pump started (%dx%d)", width, height);
}

static void stopSoftwareFramePump() {
    if (g_pumpRunning.exchange(false)) {  // returns old value; join only if was running
        if (g_framePumpThread.joinable()) {
            g_framePumpThread.join();
        }
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
    // Check /bin/ first (flattened GSI rootfs), then /system/bin/.
    std::string linkerPath = resolveRootfsPath(root, {
        "bin/linker64",
        "system/bin/linker64",
    });
    if (linkerPath.empty()) {
        GL_LOGE("linker64 not found under %s", root.c_str());
        mState = ContainerState::STOPPED;
        return false;
    }

    // ---- Resolve the guest entry-point binary ----
    // Check /bin/ first (flattened layout), then /system/bin/.
    std::string binaryPath;
    if (!rel.empty()) {
        binaryPath = resolveRootfsPath(root, {rel.c_str()});
    }
    if (binaryPath.empty()) {
        binaryPath = resolveRootfsPath(root, {
            "bin/sh",
            "system/bin/sh",
        });
    }
    if (binaryPath.empty()) {
        GL_LOGE("no guest binary found under %s", root.c_str());
        mState = ContainerState::STOPPED;
        return false;
    }

    GL_LOGI("linker:  %s", linkerPath.c_str());
    GL_LOGI("binary:  %s", binaryPath.c_str());

    mState = ContainerState::STARTING;

    /* Create a pipe so the child's stdout+stderr are captured to logcat.
     * The child writes to logPipe[1]; the parent reads from logPipe[0]. */
    int logPipe[2] = {-1, -1};
    if (pipe(logPipe) != 0) {
        GL_LOGE("pipe() failed: %s", strerror(errno));
        mState = ContainerState::STOPPED;
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        mState = ContainerState::STOPPED;
        return false;
    }

    if (pid == 0) {
        /* ---- child ---- */
        setpgid(0, 0);
        chdir(root.c_str());

        /* Redirect stdin to /dev/null; stdout+stderr go to the log pipe. */
        int devNull = open("/dev/null", O_RDONLY);
        if (devNull >= 0) { dup2(devNull, STDIN_FILENO);  close(devNull); }
        dup2(logPipe[1], STDOUT_FILENO);
        dup2(logPipe[1], STDERR_FILENO);
        /* Close all inherited fds above stderr. */
        for (int fd = 3; fd < 256; ++fd) close(fd);

        std::vector<std::string> envStrings = {
            "LD_PRELOAD="       + config.libfakePath,
            /* lib64/lib before system/lib64 — flattened rootfs first. */
            "LD_LIBRARY_PATH=" + root + "/lib64:"
                               + root + "/lib:"
                               + root + "/system/lib64:"
                               + root + "/system/lib:"
                               + root + "/apex/com.android.runtime/lib64",
            "AOSP_ROOTFS_DIR=" + root,
            /* /bin before /system/bin — flattened rootfs first. */
            "PATH="            + root + "/bin:" + root + "/system/bin:/system/bin",
            /* Bionic uses ANDROID_ROOT to locate property files and
             * runtime resources — point to the container root itself
             * so it works with both flattened and /system layouts. */
            "ANDROID_ROOT="    + root,
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

    /* ---- parent ---- */
    /* Close the write end — only the child writes to it. */
    close(logPipe[1]);

    /* Spawn a reader thread that streams the child's output to logcat. */
    if (g_logPipeThread.joinable()) g_logPipeThread.join();
    int readFd = logPipe[0];
    g_logPipeThread = std::thread([readFd]() {
        char buf[256];
        std::string line;
        ssize_t n;
        while ((n = ::read(readFd, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            for (ssize_t i = 0; i < n; ++i) {
                if (buf[i] == '\n') {
                    if (!line.empty()) {
                        GL_LOGI("[guest] %s", line.c_str());
                        line.clear();
                    }
                } else if (buf[i] != '\r') {
                    line += buf[i];
                }
            }
        }
        if (!line.empty()) GL_LOGI("[guest] %s", line.c_str());
        ::close(readFd);
    });

    mGuestPid = pid;
    mFrameFd  = config.frameFd;
    mState = ContainerState::RUNNING;
    if (mMonitorThread.joinable()) mMonitorThread.join();
    mMonitorThread = std::thread(&GuestLauncher::monitorLoop, this, pid);
    GL_LOGI("guest started pid=%d via linker64", pid);

    /* Start the software frame pump so the EGL/GL pipeline is exercised
     * immediately, before the guest compositor outputs real frames. */
    startSoftwareFramePump(config.frameWidth, config.frameHeight);

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
