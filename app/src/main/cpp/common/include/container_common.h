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

/* Default relative location of instances under the app files dir. */
#define AC_INSTANCE_DIR "rootfs"

#endif /* CONTAINER_COMMON_H */
