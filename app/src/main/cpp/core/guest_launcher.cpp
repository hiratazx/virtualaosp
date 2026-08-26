#include "guest_launcher.h"
#include <android/log.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define GL_TAG "ac.guest"
#define GL_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  GL_TAG, __VA_ARGS__)
#define GL_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, GL_TAG, __VA_ARGS__)

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

    // ---- Resolve binary path in the parent (async-signal-safe checks
    //      cannot run reliably in the child before execve). ----
    const std::string& root = config.rootfsPath;
    const std::string& rel  = config.initBinaryPath;

    // Strip any leading '/' so we never get double-slashes.
    std::string binaryPath = root + "/" +
        ((!rel.empty() && rel.front() == '/') ? rel.substr(1) : rel);

    // Fallback chain: requested binary → /system/bin/sh → /bin/sh
    if (access(binaryPath.c_str(), X_OK) != 0) {
        GL_LOGE("target binary not executable: %s (errno %d: %s) — trying fallbacks",
                binaryPath.c_str(), errno, strerror(errno));
        const char* fallbacks[] = {"/system/bin/sh", "/bin/sh", nullptr};
        bool found = false;
        for (int i = 0; fallbacks[i]; ++i) {
            std::string fb = root + fallbacks[i];
            if (access(fb.c_str(), X_OK) == 0) {
                GL_LOGI("falling back to %s", fb.c_str());
                binaryPath = fb;
                found = true;
                break;
            }
        }
        if (!found) {
            GL_LOGE("no executable guest binary found under %s", root.c_str());
            mState = ContainerState::STOPPED;
            return false;
        }
    }

    // Repair execute bit if missing (common on freshly extracted tar archives).
    if (chmod(binaryPath.c_str(), 0755) != 0) {
        GL_LOGI("chmod 0755 failed for %s: %s (continuing)",
                binaryPath.c_str(), strerror(errno));
    }

    GL_LOGI("launching guest: %s", binaryPath.c_str());

    mState = ContainerState::STARTING;
    pid_t pid = fork();
    if (pid < 0) {
        mState = ContainerState::STOPPED;
        return false;
    }

    if (pid == 0) {
        setpgid(0, 0);
        chdir(root.c_str());

        std::vector<std::string> envStrings = {
            "LD_PRELOAD="    + config.libfakePath,
            "LD_LIBRARY_PATH=" + root + "/system/lib64:"
                               + root + "/system/lib:"
                               + root + "/apex/com.android.runtime/lib64",
            "AOSP_ROOTFS_DIR=" + root,
            "PATH=/system/bin:/system/xbin:/bin:/apex/com.android.runtime/bin",
            "ANDROID_ROOT=/system",
            "ANDROID_DATA=/data",
        };
        std::vector<char*> envp;
        for (const auto& s : envStrings) envp.push_back(const_cast<char*>(s.c_str()));
        envp.push_back(nullptr);

        char* const argv[] = { const_cast<char*>(binaryPath.c_str()), nullptr };
        execve(binaryPath.c_str(), argv, envp.data());
        // If we reach here execve failed; log then exit so the parent's
        // waitpid sees a non-zero status and the monitor fires CRASHED.
        __android_log_print(ANDROID_LOG_ERROR, GL_TAG,
            "guest execve failed: %s — %s", binaryPath.c_str(), strerror(errno));
        _exit(127);
    }

    mGuestPid = pid;
    mState = ContainerState::RUNNING;
    if (mMonitorThread.joinable()) mMonitorThread.join();
    mMonitorThread = std::thread(&GuestLauncher::monitorLoop, this, pid);
    GL_LOGI("guest started pid=%d", pid);
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
    std::lock_guard<std::mutex> lock(mMutex);
    if (mGuestPid <= 0) return false;
    kill(-mGuestPid, signal);
    return true;
}
