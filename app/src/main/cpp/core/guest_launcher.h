#pragma once
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <sys/types.h>

enum class ContainerState { STOPPED = 0, STARTING = 1, RUNNING = 2, CRASHED = 3, TERMINATED = 4 };

struct LaunchConfig {
    std::string rootfsPath;
    std::string libfakePath;
    std::string initBinaryPath;
};

using StateCallback = std::function<void(ContainerState, int)>;

class GuestLauncher {
public:
    static GuestLauncher& getInstance();
    bool startContainer(const LaunchConfig& config);
    bool stopContainer(int signal = 15, int timeoutMs = 2000);
    void setStateCallback(StateCallback callback);
private:
    GuestLauncher() = default;
    void monitorLoop(pid_t pid);
    pid_t mGuestPid{-1};
    std::atomic<ContainerState> mState{ContainerState::STOPPED};
    std::thread mMonitorThread;
    StateCallback mStateCallback;
    std::mutex mMutex;
};
