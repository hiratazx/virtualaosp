#include "launcher.h"
#include "container_common.h"
#include "frame_protocol.h"
#include "log.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#include <android/log.h>
#include <fcntl.h>
#include <sys/wait.h>

#define GUEST_LOG_TAG "GuestConsole"

using accore::ContainerState;
using accore::GuestLauncher;
using accore::LaunchConfig;

namespace {

std::atomic<ContainerState> g_state{ContainerState::kIdle};
std::atomic<pid_t> g_pid{0};

/* Environment block handed to the guest image. */
class EnvBuilder {
public:
    void Set(const std::string& key, const std::string& value) {
        entries_.emplace_back(key + "=" + value);
    }

    char* const* Build() {
        for (auto& e : entries_) {
            pointers_.push_back(e.data());
        }
        pointers_.push_back(nullptr);
        return pointers_.data();
    }

private:
    std::vector<std::string> entries_;
    std::vector<char*> pointers_;
};

void ReportExecFailure(int* pipe_fd, int err) {
    ssize_t n = write(pipe_fd[1], &err, sizeof(err));
    (void)n;
    _exit(127);
}

} // namespace

namespace {

/* Reads lines from fd and forwards each to logcat. Exits when the write
 * end of the pipe is closed (guest exited). Runs on a detached thread. */
void logcatPump(int read_fd) {
    char buf[4096];
    std::string line;
    ssize_t n;
    while ((n = read(read_fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                __android_log_print(ANDROID_LOG_INFO, GUEST_LOG_TAG, "%s", line.c_str());
                line.clear();
            } else {
                line += buf[i];
            }
        }
    }
    if (!line.empty()) {
        __android_log_print(ANDROID_LOG_INFO, GUEST_LOG_TAG, "%s", line.c_str());
    }
    close(read_fd);
}

} // namespace

namespace accore {

pid_t GuestLauncher::Start(const LaunchConfig& cfg) {
    if (cfg.rootfs_dir.empty() || cfg.init_path.empty()) {
        return -EINVAL;
    }
    if (cfg.init_path.front() != '/') {
        return -EINVAL;
    }

    /* libfake.so must ship inside our APK's native lib dir. */
    const std::string preload = cfg.native_lib_dir + "/libfake.so";

    int err_pipe[2];
    if (pipe2(err_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        return -errno;
    }

    /* Stdout/stderr capture pipe for guest boot diagnostics. */
    int log_pipe[2];
    if (pipe2(log_pipe, O_CLOEXEC) != 0) {
        close(err_pipe[0]);
        close(err_pipe[1]);
        return -errno;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(err_pipe[0]);
        close(err_pipe[1]);
        close(log_pipe[0]);
        close(log_pipe[1]);
        return -errno;
    }

    if (pid == 0) {
        /* ---- child: async-signal-safe code until execve ---- */
        close(err_pipe[0]);
        /* Redirect stdout + stderr into the diagnostic log pipe so the
         * parent's logcatPump thread can forward boot output to logcat. */
        close(log_pipe[0]);
        dup2(log_pipe[1], STDOUT_FILENO);
        dup2(log_pipe[1], STDERR_FILENO);
        close(log_pipe[1]);
        setsid();

        const std::string init_abs = cfg.rootfs_dir + cfg.init_path;

        EnvBuilder env;
        env.Set(AC_ENV_ENABLED, "1");
        env.Set(AC_ENV_ROOTFS, cfg.rootfs_dir);
        env.Set(AC_ENV_FAKE_UID, std::to_string(cfg.fake_uid));
        env.Set(AC_ENV_FAKE_GID, std::to_string(cfg.fake_gid));
        if (cfg.enable_seccomp) env.Set(AC_ENV_SECCOMP, "1");
        if (cfg.frame_fd >= 0) {
            env.Set(AC_ENV_FRAME_FD, std::to_string(cfg.frame_fd));
        }
        if (!cfg.extra_mounts.empty()) env.Set(AC_ENV_MOUNTS, cfg.extra_mounts);
        if (!cfg.exclude_paths.empty()) env.Set(AC_ENV_EXCLUDE, cfg.exclude_paths);
        env.Set("LD_PRELOAD", preload);
        env.Set("PATH", "/system/bin:/system/xbin:/vendor/bin");
        env.Set("ANDROID_ROOT", "/system");
        env.Set("ANDROID_DATA", "/data");
        env.Set("HOME", "/");

        char argv0[] = "init";
        char* argv[] = {argv0, nullptr};

        execve(init_abs.c_str(), argv, env.Build());
        ReportExecFailure(err_pipe, errno);
    }

    /* ---- parent ---- */
    close(err_pipe[1]);
    /* Close the write end so the pump thread sees EOF when the guest exits. */
    close(log_pipe[1]);

    /* Spin up the logcat pump thread; it owns log_pipe[0] and closes it. */
    std::thread(logcatPump, log_pipe[0]).detach();

    int child_errno = 0;
    ssize_t n = read(err_pipe[0], &child_errno, sizeof(child_errno));
    close(err_pipe[0]);
    if (n == static_cast<ssize_t>(sizeof(child_errno))) {
        /* CLOEXEC pipe survived => exec failed and the child is gone. */
        int st;
        waitpid(pid, &st, 0);
        AC_LOGE("guest exec failed: %s", strerror(child_errno));
        return -(child_errno > 0 ? child_errno : EIO);
    }

    SetCurrentPid(pid);
    SetState(ContainerState::kRunning);
    AC_LOGI("guest started pid=%d root=%s", static_cast<int>(pid), cfg.rootfs_dir.c_str());
    return pid;
}

bool GuestLauncher::Stop(pid_t pid, int grace_ms) {
    if (pid <= 0) return false;

    SetState(ContainerState::kStopping);
    kill(-pid, SIGTERM); /* process group created via setsid() */
    kill(pid, SIGTERM);

    const int poll_interval_ms = 50;
    int waited = 0;
    while (waited < grace_ms) {
        if (kill(pid, 0) != 0 && errno == ESRCH) {
            SetCurrentPid(0);
            SetState(ContainerState::kIdle);
            return true;
        }
        usleep(poll_interval_ms * 1000);
        waited += poll_interval_ms;
    }

    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);

    int status = 0;
    waitpid(pid, &status, 0);
    SetCurrentPid(0);
    SetState(ContainerState::kIdle);
    AC_LOGI("guest pid=%d killed after %dms grace", static_cast<int>(pid), grace_ms);
    return true;
}

ContainerState GuestLauncher::state() {
    return g_state.load(std::memory_order_relaxed);
}

void GuestLauncher::SetState(ContainerState s) {
    g_state.store(s, std::memory_order_relaxed);
}

pid_t GuestLauncher::current_pid() {
    return g_pid.load(std::memory_order_relaxed);
}

void GuestLauncher::SetCurrentPid(pid_t pid) {
    g_pid.store(pid, std::memory_order_relaxed);
}

} // namespace accore
