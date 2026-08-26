/*
 * libfake.so — LD_PRELOAD interception engine for the unrooted container.
 *
 * Loaded by the dynamic linker into every guest process before libc-using
 * code runs. Exported symbols shadow their bionic counterparts, redirecting
 * filesystem traffic into the host sandbox and spoofing process identity.
 *
 * Phase 1.1: bootstrap only — activation detection and API version marker.
 */
#include "container_common.h"
#include "log.h"

#include <cstdlib>
#include <cstring>

#define AC_EXPORT __attribute__((visibility("default")))

namespace {

volatile bool g_enabled = false;

char g_rootfs[4096] = {0};

long g_fake_uid = 0;
long g_fake_gid = 0;

} // namespace

extern "C" {

/* True when the process was launched inside the container. */
AC_EXPORT bool ac_fake_enabled(void) {
    return g_enabled;
}

/* Host-side sandbox root backing the guest rootfs ("" when inactive). */
AC_EXPORT const char* ac_fake_rootfs(void) {
    return g_rootfs;
}

AC_EXPORT long ac_fake_uid_value(void) {
    return g_fake_uid;
}

AC_EXPORT long ac_fake_gid_value(void) {
    return g_fake_gid;
}

AC_EXPORT int ac_fake_api_version(void) {
    return 1; /* bump on ABI-changing changes to the hook surface */
}

} /* extern "C" */

__attribute__((constructor)) static void ac_fake_init(void) {
    const char* enabled = getenv(AC_ENV_ENABLED);
    if (enabled == nullptr || enabled[0] != '1') {
        return; /* Host-side process: hooks must stay fully transparent. */
    }

    const char* rootfs = getenv(AC_ENV_ROOTFS);
    if (rootfs == nullptr || rootfs[0] != '/') {
        AC_LOGE("enabled but %s missing or relative; staying dormant", AC_ENV_ROOTFS);
        return;
    }

    size_t len = strlen(rootfs);
    if (len == 0 || len >= sizeof(g_rootfs)) {
        AC_LOGE("rootfs path length invalid");
        return;
    }
    memcpy(g_rootfs, rootfs, len + 1);

    const char* uid = getenv(AC_ENV_FAKE_UID);
    const char* gid = getenv(AC_ENV_FAKE_GID);
    g_fake_uid = uid ? strtol(uid, nullptr, 10) : 0;
    g_fake_gid = gid ? strtol(gid, nullptr, 10) : 0;

    g_enabled = true;
    AC_LOGI("libfake active: root=%s uid=%ld gid=%ld", g_rootfs, g_fake_uid, g_fake_gid);
}
