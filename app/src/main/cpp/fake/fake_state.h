/*
 * Engine-wide state shared by every libfake subsystem (redirection, uid
 * spoofing, vfs emulation). Populated once by the load-time constructor
 * from environment variables injected by the container launcher.
 */
#ifndef FAKE_STATE_H
#define FAKE_STATE_H

#include <stddef.h>

namespace acfake {

/* True when running inside a container-launched guest process. */
bool enabled();

/* Host-side sandbox root backing the guest "/" (never null; empty if off). */
const char* rootfs();
size_t rootfs_len();

/* Spoofed identity reported to the guest (defaults to uid 0 / gid 0). */
long fake_uid();
long fake_gid();

/* Parse AC_* environment variables. Called once from the ELF constructor. */
void init_from_env();

/* Explicit state injection (used by tests and the seccomp coordinator). */
void set_state(bool enabled, const char* rootfs, long uid, long gid);

} // namespace acfake

#endif /* FAKE_STATE_H */
