#include "fake_state.h"
#include "container_common.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

namespace acfake {

namespace {

bool g_enabled = false;

char g_rootfs[4096];
size_t g_rootfs_len = 0;

long g_fake_uid = 0;
long g_fake_gid = 0;

} // namespace

bool enabled() {
    return g_enabled;
}

const char* rootfs() {
    return g_rootfs;
}

size_t rootfs_len() {
    return g_rootfs_len;
}

long fake_uid() {
    return g_fake_uid;
}

long fake_gid() {
    return g_fake_gid;
}

void set_state(bool enable, const char* root, long uid, long gid) {
    g_rootfs[0] = '\0';
    g_rootfs_len = 0;
    if (enable && root != nullptr && root[0] == '/') {
        size_t len = strlen(root);
        while (len > 1 && root[len - 1] == '/') {
            --len; /* normalize trailing slashes */
        }
        if (len >= sizeof(g_rootfs)) {
            AC_LOGE("rootfs path exceeds %zu bytes; refusing activation", sizeof(g_rootfs));
            return;
        }
        memcpy(g_rootfs, root, len);
        g_rootfs[len] = '\0';
        g_rootfs_len = len;
        g_enabled = true;
    } else {
        g_enabled = false;
    }
    g_fake_uid = uid;
    g_fake_gid = gid;
}

void init_from_env() {
    const char* flag = getenv(AC_ENV_ENABLED);
    if (flag == nullptr || flag[0] != '1') {
        return; /* host process: remain fully transparent */
    }

    const char* uid = getenv(AC_ENV_FAKE_UID);
    const char* gid = getenv(AC_ENV_FAKE_GID);

    set_state(true, getenv(AC_ENV_ROOTFS),
              uid ? strtol(uid, nullptr, 10) : 0,
              gid ? strtol(gid, nullptr, 10) : 0);

    if (g_enabled) {
        AC_LOGI("libfake active: root=%s uid=%ld gid=%ld", g_rootfs, g_fake_uid, g_fake_gid);
    }
}

} // namespace acfake
