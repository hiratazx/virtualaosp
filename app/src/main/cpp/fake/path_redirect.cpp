#include "path_redirect.h"
#include "fake_state.h"

#include <string.h>

namespace acfake {

namespace {

/* Virtual filesystems served from userspace, never backed by the sandbox. */
constexpr const char* kVirtualFsPrefixes[] = {
    "/proc",
    "/sys",
};

constexpr size_t kMaxExcludes = 16;
constexpr size_t kMaxExcludeLen = 256;

struct ExcludeTable {
    char entries[kMaxExcludes][kMaxExcludeLen];
    size_t count = 0;
};

ExcludeTable g_excludes;

bool has_prefix(const char* path, const char* prefix) {
    const size_t n = strlen(prefix);
    if (strncmp(path, prefix, n) != 0) {
        return false;
    }
    /* "/proc" matches "/proc/x" but not "/processor_info". */
    return path[n] == '/' || path[n] == '\0';
}

bool starts_with_rootfs(const char* path) {
    const size_t len = rootfs_len();
    if (len == 0) {
        return false;
    }
    if (strncmp(path, rootfs(), len) != 0) {
        return false;
    }
    return path[len] == '\0' || path[len] == '/';
}

} // namespace

void set_excluded_prefixes(const char* colon_list) {
    if (colon_list == nullptr) {
        return;
    }
    size_t idx = 0;
    const char* p = colon_list;
    while (*p != '\0' && g_excludes.count < kMaxExcludes) {
        const char* end = strchr(p, ':');
        const size_t seg = (end != nullptr) ? static_cast<size_t>(end - p) : strlen(p);
        if (seg > 0 && seg < kMaxExcludeLen && p[0] == '/') {
            memcpy(g_excludes.entries[idx], p, seg);
            g_excludes.entries[idx][seg] = '\0';
            ++idx;
            ++g_excludes.count;
        }
        if (end == nullptr) {
            break;
        }
        p = end + 1;
    }
}

bool is_virtual_fs_path(const char* path) {
    for (const char* prefix : kVirtualFsPrefixes) {
        if (has_prefix(path, prefix)) {
            return true;
        }
    }
    return false;
}

MapResult map_path(const char* path, char* out, size_t out_size) {
    if (!enabled() || path == nullptr || path[0] != '/') {
        return MapResult::Passthrough;
    }

    /* Idempotency guard: sandbox-internal traffic flows untouched. */
    if (starts_with_rootfs(path)) {
        return MapResult::Passthrough;
    }

    /* Userspace-served trees take precedence over raw redirection. */
    if (is_virtual_fs_path(path)) {
        return MapResult::Passthrough;
    }

    for (size_t i = 0; i < g_excludes.count; ++i) {
        if (has_prefix(path, g_excludes.entries[i])) {
            return MapResult::Passthrough;
        }
    }

    const size_t plen = strlen(path);
    const size_t rlen = rootfs_len();
    if (rlen + plen + 1 > out_size) {
        return MapResult::Overflow;
    }

    memcpy(out, rootfs(), rlen);
    memcpy(out + rlen, path, plen + 1);
    return MapResult::Mapped;
}

} // namespace acfake
