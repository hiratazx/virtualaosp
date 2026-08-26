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
    /* Optional shared-memory frame channel fd from ContainerCore.
     * -1 disables the software frame pump. */
    int frameFd{-1};
    int frameWidth{720};
    int frameHeight{1280};
    int frameSlots{4};
};

using StateCallback = std::function<void(ContainerState, int)>;

class GuestLauncher {
public:
    static GuestLauncher& getInstance();
    bool startContainer(const LaunchConfig& config);
    bool stopContainer(int signal = 15, int timeoutMs = 2000);
    void setStateCallback(StateCallback callback);
    ContainerState state() const { return mState.load(std::memory_order_relaxed); }
private:
    GuestLauncher() = default;
    void monitorLoop(pid_t pid);
    pid_t mGuestPid{-1};
    int   mFrameFd{-1};
    std::atomic<ContainerState> mState{ContainerState::STOPPED};
    std::thread mMonitorThread;
    StateCallback mStateCallback;
    std::mutex mMutex;
};
