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
#include "frame_producer.h"
#include "log.h"
#include "path_redirect.h"
#include "seccomp_filter.h"

#include <cstdlib>
#include <cstring>

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
    return 3; /* bump on ABI-changing changes to the hook surface */
}

} /* extern "C" */

__attribute__((constructor)) static void ac_fake_init(void) {
    acfake::init_from_env();
    if (!acfake::enabled()) {
        return;
    }

    acfake::set_excluded_prefixes(getenv(AC_ENV_EXCLUDE));

    acfake::frame_producer_init_from_env();

    if (const char* sc = getenv(AC_ENV_SECCOMP); sc != nullptr && sc[0] == '1') {
        acfake::install_seccomp_filter();
    }
}
