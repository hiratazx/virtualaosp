/*
 * Guest process coordinator.
 *
 * Spawns the guest init tree with the interception library injected via
 * LD_PRELOAD and the AC_* environment contract understood by libfake.so.
 *
 * Executability note: guest ELFs carry a PT_INTERP pointing at a
 * container-aware linker path (absolute, inside the sandbox). Producing
 * such rootfs images is the RootFS importer's job (Phase 5); the launcher
 * simply executes <rootfs>/<init_path> and reports exec failures back
 * through a CLOEXEC pipe.
 */
#ifndef LAUNCHER_H
#define LAUNCHER_H

#include <sys/types.h>

#include <string>

namespace accore {

struct LaunchConfig {
    std::string rootfs_dir;    /* host-side sandbox instance dir */
    std::string native_lib_dir;/* ABI dir holding libfake.so */
    std::string init_path;     /* guest-relative, e.g. "/init" */
    std::string extra_mounts;  /* verbatim lines appended to emulated /proc/mounts */
    std::string exclude_paths; /* colon list of passthrough prefixes */
    int fake_uid = 0;
    int fake_gid = 0;
    bool enable_seccomp = false;
};

enum class ContainerState {
    kIdle = 0,
    kStarting,
    kRunning,
    kStopping,
    kExited,
};

class GuestLauncher {
public:
    /*
     * Fork+exec one guest process. Returns:
     *   >0  child pid on success
     *   <0  -errno style failure (parent-side fork/exec setup errors,
     *        or child exec failure relayed over the CLOEXEC pipe)
     */
    static pid_t Start(const LaunchConfig& cfg);

    /* SIGTERM the process group; escalate to SIGKILL after grace_ms. */
    static bool Stop(pid_t pid, int grace_ms);

    static ContainerState state();
    static void SetState(ContainerState s);
    static pid_t current_pid();
    static void SetCurrentPid(pid_t pid);
};

} // namespace accore

#endif /* LAUNCHER_H */
