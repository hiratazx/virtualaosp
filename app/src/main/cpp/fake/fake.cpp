/*
 * libfake.so — LD_PRELOAD interception engine for the unrooted container.
 *
 * Loaded by the dynamic linker into every guest process before libc-using
 * code runs. Exported symbols shadow their bionic counterparts, redirecting
 * filesystem traffic into the host sandbox and spoofing process identity.
 *
 * Subsystems register work in their own translation units; this TU owns the
 * load-time constructor and the stable C query API.
 */
#include "container_common.h"
#include "fake_state.h"
#include "log.h"

extern "C" {

__attribute__((visibility("default")))
bool ac_fake_enabled(void) {
    return acfake::enabled();
}

__attribute__((visibility("default")))
const char* ac_fake_rootfs(void) {
    return acfake::rootfs();
}

__attribute__((visibility("default")))
long ac_fake_uid_value(void) {
    return acfake::fake_uid();
}

__attribute__((visibility("default")))
long ac_fake_gid_value(void) {
    return acfake::fake_gid();
}

__attribute__((visibility("default")))
int ac_fake_api_version(void) {
    return 2; /* bump on ABI-changing changes to the hook surface */
}

} /* extern "C" */

__attribute__((constructor)) static void ac_fake_init(void) {
    acfake::init_from_env();
}
