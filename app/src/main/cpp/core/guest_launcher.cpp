#include "guest_launcher.h"
#include <unistd.h>
#include <sys/wait.h>
#include <csignal>

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

    mState = ContainerState::STARTING;
    pid_t pid = fork();
    if (pid < 0) {
        mState = ContainerState::STOPPED;
        return false;
    }

    if (pid == 0) {
        setpgid(0, 0);
        chdir(config.rootfsPath.c_str());

        std::vector<std::string> envStrings = {
            "LD_PRELOAD=" + config.libfakePath,
            "AOSP_ROOTFS_DIR=" + config.rootfsPath,
            "PATH=" + config.rootfsPath + "/system/bin:" + config.rootfsPath + "/system/xbin",
            "ANDROID_ROOT=/system",
            "ANDROID_DATA=/data"
        };
        std::vector<char*> envp;
        for (const auto& s : envStrings) envp.push_back(const_cast<char*>(s.c_str()));
        envp.push_back(nullptr);

        std::string binary = config.rootfsPath + config.initBinaryPath;
        char* const argv[] = { const_cast<char*>(binary.c_str()), nullptr };

        execve(binary.c_str(), argv, envp.data());
        _exit(127);
    }

    mGuestPid = pid;
    mState = ContainerState::RUNNING;
    if (mMonitorThread.joinable()) mMonitorThread.join();
    mMonitorThread = std::thread(&GuestLauncher::monitorLoop, this, pid);
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
