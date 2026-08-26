/*
 * Pseudo-mount layer: guest mount operations succeed virtually without
 * ever touching the host mount table. Device nodes become regular
 * placeholder files inside the sandbox (unrooted processes cannot create
 * real device nodes anyway).
 */
#include "fake_state.h"
#include "path_redirect.h"
#include "log.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define AC_EXPORT __attribute__((visibility("default")))

using namespace acfake;

namespace {

void* next_sym(void** slot, const char* name) {
    void* fn = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (fn == nullptr) {
        fn = dlsym(RTLD_NEXT, name);
        if (fn == nullptr) {
            AC_LOGE("dlsym(RTLD_NEXT, %s) failed", name);
            abort();
        }
        __atomic_store_n(slot, fn, __ATOMIC_RELEASE);
    }
    return fn;
}

constexpr size_t kPathBufSize = 8192;

/* mknod emulation: create a placeholder REGULAR file. Faithful errno
 * semantics: EEXIST when something already occupies the path. */
int emu_mknod(const char* mapped_path, mode_t /*mode*/) {
    if (mapped_path == nullptr) {
        errno = EINVAL;
        return -1;
    }
    static void* s_open;
    using OpenFn = int (*)(const char*, int, mode_t);
    int fd = reinterpret_cast<OpenFn>(next_sym(&s_open, "open"))(
        mapped_path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    if (fd >= 0) {
        static void* s_close;
        using CloseFn = int (*)(int);
        reinterpret_cast<CloseFn>(next_sym(&s_close, "close"))(fd);
        return 0;
    }
    if (errno == EEXIST) {
        struct stat st;
        static void* s_stat;
        using StatFn = int (*)(const char*, struct stat*);
        if (reinterpret_cast<StatFn>(next_sym(&s_stat, "stat"))(mapped_path, &st) == 0 &&
            S_ISREG(st.st_mode)) {
            return 0; /* idempotent re-creation of a placeholder */
        }
        errno = EEXIST;
        return -1;
    }
    return -1;
}

} // namespace

/* ------------------------------------------------------------------ */

AC_EXPORT int mount(const char* source, const char* target, const char* filesystemtype,
                    unsigned long mountflags, const void* data) {
    if (!enabled()) {
        static void* s;
        using Fn = int (*)(const char*, const char*, const char*, unsigned long, const void*);
        return reinterpret_cast<Fn>(next_sym(&s, "mount"))(source, target, filesystemtype,
                                                           mountflags, data);
    }
    AC_LOGD("fake mount: src=%s tgt=%s fstype=%s flags=0x%lx",
            source ? source : "(null)", target ? target : "(null)",
            filesystemtype ? filesystemtype : "(null)", mountflags);
    return 0;
}

AC_EXPORT int umount(const char* target) {
    if (!enabled()) {
        static void* s;
        using Fn = int (*)(const char*);
        return reinterpret_cast<Fn>(next_sym(&s, "umount"))(target);
    }
    AC_LOGD("fake umount: %s", target ? target : "(null)");
    return 0;
}

AC_EXPORT int umount2(const char* target, int flags) {
    if (!enabled()) {
        static void* s;
        using Fn = int (*)(const char*, int);
        return reinterpret_cast<Fn>(next_sym(&s, "umount2"))(target, flags);
    }
    AC_LOGD("fake umount2: %s flags=%d", target ? target : "(null)", flags);
    return 0;
}

AC_EXPORT int pivot_root(const char* new_root, const char* put_old) {
    if (!enabled()) {
        static void* s;
        using Fn = int (*)(const char*, const char*);
        return reinterpret_cast<Fn>(next_sym(&s, "pivot_root"))(new_root, put_old);
    }
    AC_LOGD("fake pivot_root: new=%s old=%s", new_root ? new_root : "(null)",
            put_old ? put_old : "(null)");
    return 0;
}

/*
 * chroot is faked rather than denied: guest init scripts commonly invoke
 * it. The VFS redirection already provides an equivalent isolation, so
 * pretending success keeps such flows alive without any real jail.
 */
AC_EXPORT int chroot(const char* path) {
    if (!enabled()) {
        static void* s;
        using Fn = int (*)(const char*);
        return reinterpret_cast<Fn>(next_sym(&s, "chroot"))(path);
    }
    AC_LOGD("fake chroot: %s", path ? path : "(null)");
    return 0;
}

AC_EXPORT int mknod(const char* path, mode_t mode, dev_t dev) {
    if (!enabled()) {
        static void* s;
        using Fn = int (*)(const char*, mode_t, dev_t);
        return reinterpret_cast<Fn>(next_sym(&s, "mknod"))(path, mode, dev);
    }
    (void)dev; /* no real device numbers inside the sandbox */
    char buf[kPathBufSize];
    MapResult r = map_path(path, buf, sizeof(buf));
    if (r == MapResult::Overflow) {
        errno = ENAMETOOLONG;
        return -1;
    }
    const char* mp = (r == MapResult::Mapped) ? buf : path;
    AC_LOGD("fake mknod: %s -> placeholder file", path);
    return emu_mknod(mp, mode);
}

AC_EXPORT int mknodat(int dirfd, const char* path, mode_t mode, dev_t dev) {
    if (!enabled()) {
        static void* s;
        using Fn = int (*)(int, const char*, mode_t, dev_t);
        return reinterpret_cast<Fn>(next_sym(&s, "mknodat"))(dirfd, path, mode, dev);
    }
    (void)dev;
    char buf[kPathBufSize];
    const char* mp = path;
    if (path != nullptr && path[0] == '/') {
        MapResult r = map_path(path, buf, sizeof(buf));
        if (r == MapResult::Overflow) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (r == MapResult::Mapped) mp = buf;
    }
    return emu_mknod(mp, mode);
}
