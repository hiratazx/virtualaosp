/*
 * Shared constants between the host coordinator (libcontainer_core) and the
 * guest interception engine (libfake). The launcher exports these environment
 * variables into every spawned guest process; libfake reads them at load time.
 */
#ifndef CONTAINER_COMMON_H
#define CONTAINER_COMMON_H

/* Set to "1" inside guest processes; absent on the host => hooks pass through. */
#define AC_ENV_ENABLED "AC_CONTAINER"

/* Host-side absolute path of the container storage root, e.g.
 * /data/user/0/dev.itzkaguya.aospcontainer/files/rootfs/<instance> */
#define AC_ENV_ROOTFS "AC_ROOTFS"

/* Fake identity reported to guest processes (defaults: uid/gid = 0). */
#define AC_ENV_FAKE_UID "AC_FAKE_UID"
#define AC_ENV_FAKE_GID "AC_FAKE_GID"

/* Comma separated list of extra guest-visible mount entries for /proc/mounts. */
#define AC_ENV_MOUNTS "AC_MOUNTS"

/* Colon separated host prefixes that bypass redirection entirely. */
#define AC_ENV_EXCLUDE "AC_EXCLUDE"

/* Set to "1" by the launcher to install the SECCOMP denylist filter. */
#define AC_ENV_SECCOMP "AC_SECCOMP"

/* Guest-visible endpoint of the host IPC daemon. */
#define AC_ENV_IPC_SOCK "AC_IPC_SOCK"
#define AC_IPC_DEFAULT_SOCK "/.host.sock"

/* Spool file where received touch events are appended for the guest
 * input framework helper (fake_input) to consume. */
#define AC_INPUT_SPOOL "/dev/ac_input"

/* Default relative location of instances under the app files dir. */
#define AC_INSTANCE_DIR "rootfs"

#endif /* CONTAINER_COMMON_H */
