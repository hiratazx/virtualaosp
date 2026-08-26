#include "vfs_emu.h"
#include "container_common.h"
#include "fake_state.h"
#include "log.h"
#include "path_redirect.h"
#include "real_libc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using namespace acfake;

namespace {

constexpr size_t kLineBuf = 4096;
constexpr mode_t kVirtFileMode = S_IFREG | 0444;
constexpr mode_t kVirtDirMode = S_IFDIR | 0555;

/* forward decls (defined further below; anonymous namespaces merge) */
bool placeholder_path(const char* path, char* out, size_t outsz);

enum class Entry {
    NotVirtual,
    Unknown,
    Dir,
    Version,
    Cpuinfo,
    Meminfo,
    Uptime,
    Filesystems,
    MountsTop,
    SelfStatus,
    SelfCmdline,
    SelfMaps,
    SelfMounts,
    SelfMountinfo,
};

Entry classify(const char* path) {
    if (!is_virtual_fs_path(path)) return Entry::NotVirtual;

    if (strcmp(path, "/proc") == 0 || strcmp(path, "/sys") == 0 ||
        strcmp(path, "/proc/self") == 0 || strcmp(path, "/proc/net") == 0 ||
        strncmp(path, "/sys/", 5) == 0) {
        /* /sys subtrees exist only as far as the placeholder tree does;
         * opendir on missing leaf dirs fails naturally with ENOENT. */
        return Entry::Dir;
    }

    struct FileMap { const char* path; Entry e; };
    static const FileMap kFiles[] = {
        {"/proc/version", Entry::Version},
        {"/proc/cpuinfo", Entry::Cpuinfo},
        {"/proc/meminfo", Entry::Meminfo},
        {"/proc/uptime", Entry::Uptime},
        {"/proc/filesystems", Entry::Filesystems},
        {"/proc/mounts", Entry::MountsTop},
        {"/proc/self/status", Entry::SelfStatus},
        {"/proc/self/cmdline", Entry::SelfCmdline},
        {"/proc/self/maps", Entry::SelfMaps},
        {"/proc/self/mounts", Entry::SelfMounts},
        {"/proc/self/mountinfo", Entry::SelfMountinfo},
    };
    for (const FileMap& fm : kFiles) {
        if (strcmp(path, fm.path) == 0) return fm.e;
    }

    /* /proc/<own-pid>/... aliases /proc/self/... */
    char pfx[32];
    snprintf(pfx, sizeof(pfx), "/proc/%d/", static_cast<int>(real_getpid()));
    size_t plen = strlen(pfx);
    if (strncmp(path, pfx, plen) == 0) {
        const char* rest = path + plen;
        if (strcmp(rest, "status") == 0) return Entry::SelfStatus;
        if (strcmp(rest, "cmdline") == 0) return Entry::SelfCmdline;
        if (strcmp(rest, "maps") == 0) return Entry::SelfMaps;
        if (strcmp(rest, "mounts") == 0) return Entry::SelfMounts;
        if (strcmp(rest, "mountinfo") == 0) return Entry::SelfMountinfo;
        if (rest[0] == '\0') return Entry::Dir;
    } else {
        snprintf(pfx, sizeof(pfx), "/proc/%d", static_cast<int>(real_getpid()));
        if (strcmp(path, pfx) == 0) return Entry::Dir;
    }
    return Entry::Unknown;
}

/* ------------------------------------------------------------------ */
/* streaming helpers                                                   */
/* ------------------------------------------------------------------ */

bool copy_file(const char* real_path, int out_fd) {
    int fd = real_open(real_path, O_RDONLY, 0);
    if (fd < 0) return false;
    char buf[kLineBuf];
    ssize_t n;
    bool ok = true;
    while ((n = real_read(fd, buf, sizeof(buf))) > 0) {
        if (write(out_fd, buf, static_cast<size_t>(n)) != n) {
            ok = false;
            break;
        }
    }
    ok = ok && (n == 0);
    real_close(fd);
    return ok;
}

void emit_mount_list(int out_fd) {
    dprintf(out_fd,
            "rootfs / rootfs rw 0 0\n"
            "/dev /dev devtmpfs rw 0 0\n"
            "/dev/pts /dev/pts devpts rw 0 0\n"
            "/proc /proc proc rw 0 0\n"
            "/sys /sys sysfs rw 0 0\n"
            "/system /system ext4 ro 0 0\n"
            "/vendor /vendor ext4 ro 0 0\n"
            "/product /product ext4 ro 0 0\n"
            "/apex /apex tmpfs ro 0 0\n"
            "/data /data ext4 rw,nosuid,nodev 0 0\n"
            "/cache /cache ext4 rw,nosuid,nodev 0 0\n");
    const char* extra = getenv(AC_ENV_MOUNTS);
    if (extra != nullptr && extra[0] != '\0') {
        dprintf(out_fd, "%s\n", extra);
    }
}

/*
 * maps sanitizer: lines referencing the sandbox are re-written to their
 * guest view; host paths outside the sandbox are dropped entirely so the
 * guest cannot enumerate host system libraries; anonymous regions and
 * pseudo-paths ([stack], [heap]) pass through.
 */
void emit_maps(int out_fd) {
    int fd = real_open("/proc/self/maps", O_RDONLY, 0);
    if (fd < 0) return;

    char line[kLineBuf];
    size_t fill = 0;
    ssize_t n;
    while ((n = real_read(fd, line + fill, sizeof(line) - 1 - fill)) > 0 ||
           (n < 0 && errno == EINTR)) {
        if (n <= 0) continue;
        fill += static_cast<size_t>(n);
        line[fill] = '\0';

        char* nl = strchr(line, '\n');
        while (nl != nullptr) {
            *nl = '\0';
            const char* p = line;

            /* locate pathname: skip addr perms offset dev inode fields */
            for (int f = 0; f < 5 && *p; ++f) {
                while (*p == ' ' || *p == '\t') ++p;
                while (*p && *p != ' ' && *p != '\t') ++p;
            }
            while (*p == ' ' || *p == '\t') ++p;

            const size_t head_len = static_cast<size_t>(p - line);
            if (*p == '/') {
                char unmapped[kLineBuf];
                if (unmap_path(p, unmapped, sizeof(unmapped))) {
                    (void)!write(out_fd, line, head_len);
                    (void)!write(out_fd, unmapped, strlen(unmapped));
                    (void)!write(out_fd, "\n", 1);
                }
                /* else: host-only mapping -> dropped */
            } else {
                (void)!write(out_fd, line, strlen(line));
                (void)!write(out_fd, "\n", 1);
            }

            size_t consumed = static_cast<size_t>(nl - line) + 1;
            memmove(line, line + consumed, fill - consumed);
            fill -= consumed;
            line[fill] = '\0';
            nl = strchr(line, '\n');
        }
        if (fill >= sizeof(line) - 1) {
            fill = 0; /* pathological long line: drop remainder */
        }
    }
    real_close(fd);
}

void stream_sanitized_status(int out_fd) {
    int fd = real_open("/proc/self/status", O_RDONLY, 0);
    if (fd < 0) return;
    FILE* in = real_fdopen(fd, "r");
    if (in == nullptr) {
        real_close(fd);
        return;
    }
    char line[kLineBuf];
    const long uid = fake_uid(), gid = fake_gid();
    while (fgets(line, sizeof(line), in) != nullptr) {
        if (strncmp(line, "Uid:", 4) == 0) {
            dprintf(out_fd, "Uid:\t%ld\t%ld\t%ld\t%ld\n", uid, uid, uid, uid);
        } else if (strncmp(line, "Gid:", 4) == 0) {
            dprintf(out_fd, "Gid:\t%ld\t%ld\t%ld\t%ld\n", gid, gid, gid, gid);
        } else if (strncmp(line, "CapI", 4) == 0 || strncmp(line, "CapP", 4) == 0 ||
                   strncmp(line, "CapE", 4) == 0 || strncmp(line, "CapB", 4) == 0 ||
                   strncmp(line, "CapA", 4) == 0) {
            const char* nl2 = strchr(line, ':');
            if (nl2 != nullptr) {
                dprintf(out_fd, "%.*s00000000ffffffff\n",
                        static_cast<int>(nl2 - line + 1), line);
            }
        } else {
            dprintf(out_fd, "%s", line);
        }
    }
    fclose(in);
}

} // namespace

/* ------------------------------------------------------------------ */
/* placeholder tree for directory listings                             */
/* ------------------------------------------------------------------ */

namespace {

void ensure_dir(const char* abs_guest_path) {
    /* Direct mkdir through our own hook: paths under the sandbox root
     * pass through unmodified; EEXIST is expected and fine. */
    if (mkdir(abs_guest_path, 0755) != 0 && errno != EEXIST) {
        AC_LOGW("placeholder mkdir %s failed: %s", abs_guest_path, strerror(errno));
    }
}

void ensure_placeholder_tree() {
    static volatile unsigned done = 0;
    if (__atomic_load_n(&done, __ATOMIC_ACQUIRE)) return;
    if (!enabled()) return;

    ensure_dir("/.virtual");
    ensure_dir("/.virtual/proc");
    ensure_dir("/.virtual/proc/self");
    ensure_dir("/.virtual/proc/net");
    ensure_dir("/.virtual/sys");
    ensure_dir("/.virtual/sys/class");
    ensure_dir("/.virtual/sys/class/net");
    ensure_dir("/.virtual/sys/class/net/lo");
    ensure_dir("/.virtual/sys/class/net/wlan0");
    ensure_dir("/.virtual/sys/devices");
    ensure_dir("/.virtual/sys/devices/system");

    struct { const char* path; } files[] = {
        {"/proc/version"}, {"/proc/cpuinfo"}, {"/proc/meminfo"},
        {"/proc/uptime"}, {"/proc/filesystems"}, {"/proc/mounts"},
        {"/proc/self/status"}, {"/proc/self/cmdline"}, {"/proc/self/maps"},
        {"/proc/self/mounts"}, {"/proc/self/mountinfo"},
    };
    for (const auto& f : files) {
        /* Create empty placeholders at their BACKING locations using the
         * real syscall: going through our own hooks would recursively
         * resolve these as virtual files instead of materializing them. */
        char backing[4096];
        if (placeholder_path(f.path, backing, sizeof(backing))) {
            int fd = real_open(backing, O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (fd >= 0) real_close(fd);
        }
    }

    __atomic_store_n(&done, 1u, __ATOMIC_RELEASE);
}

/* "<rootfs>/.virtual" + guest path -> sandbox backing location. */
bool placeholder_path(const char* path, char* out, size_t outsz) {
    const size_t rlen = rootfs_len();
    constexpr const char* kVirt = "/.virtual";
    const size_t vlen = strlen(kVirt);
    const size_t plen = strlen(path);
    if (rlen + vlen + plen + 1 > outsz) return false;
    memcpy(out, rootfs(), rlen);
    memcpy(out + rlen, kVirt, vlen);
    memcpy(out + rlen + vlen, path, plen + 1);
    return true;
}

} // namespace

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

namespace acfake {

int emu_open(const char* path) {
    Entry e = classify(path);
    switch (e) {
        case Entry::NotVirtual:
            return -2;
        case Entry::Unknown:
        case Entry::Dir:
            errno = ENOENT;
            return -1;
        default:
            break;
    }

    int fd = memfd_create("acvfs", 0);
    if (fd < 0) {
        AC_LOGE("memfd_create failed: %s", strerror(errno));
        return -1;
    }

    switch (e) {
        case Entry::Version:      copy_file("/proc/version", fd); break;
        case Entry::Cpuinfo:      copy_file("/proc/cpuinfo", fd); break;
        case Entry::Meminfo:      copy_file("/proc/meminfo", fd); break;
        case Entry::Uptime:       copy_file("/proc/uptime", fd); break;
        case Entry::Filesystems:  copy_file("/proc/filesystems", fd); break;
        case Entry::MountsTop:
        case Entry::SelfMounts:
        case Entry::SelfMountinfo:
            emit_mount_list(fd);
            break;
        case Entry::SelfStatus:
            stream_sanitized_status(fd);
            break;
        case Entry::SelfMaps:
            emit_maps(fd);
            break;
        case Entry::SelfCmdline:
            copy_file("/proc/self/cmdline", fd);
            break;
        default:
            break;
    }

    real_lseek(fd, 0, SEEK_SET);
    return fd;
}

int emu_stat(const char* path, struct stat* st) {
    Entry e = classify(path);
    if (e == Entry::NotVirtual) return -2;
    if (e == Entry::Unknown) {
        errno = ENOENT;
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = (e == Entry::Dir) ? kVirtDirMode : kVirtFileMode;
    st->st_nlink = 1;
    st->st_uid = static_cast<uid_t>(fake_uid());
    st->st_gid = static_cast<gid_t>(fake_gid());
    st->st_size = (e == Entry::Dir) ? 512 : 4096;
    return 0;
}

bool emu_access(const char* path, int mode, int* err_out) {
    Entry e = classify(path);
    if (e == Entry::NotVirtual) return false;
    if (e == Entry::Unknown) {
        *err_out = ENOENT;
        return true;
    }
    *err_out = (mode & W_OK) ? EACCES : 0; /* virtual trees are read-only */
    return true;
}

bool emu_readlink(const char* path, char* buf, size_t size, ssize_t* len_out) {
    if (!is_virtual_fs_path(path)) return false;

    if (strcmp(path, "/proc/self/exe") == 0 ||
        strcmp(path, "/proc/self/cwd") == 0 ||
        strcmp(path, "/proc/self/root") == 0) {
        if (strcmp(path, "/proc/self/root") == 0) {
            *len_out = static_cast<ssize_t>(strlcpy(buf, "/", size));
            return true;
        }
        char host[4096];
        ssize_t n = real_readlink(path, host, sizeof(host) - 1);
        if (n <= 0) return false;
        host[n] = '\0';
        if (!unmap_path(host, buf, size)) {
            /* Not under sandbox (pre-init): report verbatim. */
            strlcpy(buf, host, size);
            *len_out = static_cast<ssize_t>(strlen(buf));
        } else {
            *len_out = static_cast<ssize_t>(strlen(buf));
        }
        return true;
    }
    errno = ENOENT;
    *len_out = -1;
    return true;
}

DIR* emu_opendir(const char* path, bool* handled) {
    *handled = false;
    if (!is_virtual_fs_path(path)) return nullptr;
    *handled = true;

    ensure_placeholder_tree();
    char backing[4096];
    if (!placeholder_path(path, backing, sizeof(backing))) {
        errno = ENAMETOOLONG;
        return nullptr;
    }
    return real_opendir(backing);
}

} // namespace acfake
