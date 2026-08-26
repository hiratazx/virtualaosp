/*
 * Dynamic linker interposition of libc pathname READ operations:
 * open family, stat family, access, readlink, directory iteration.
 *
 * Every hook resolves its libc counterpart once via dlsym(RTLD_NEXT),
 * translates path arguments through acfake::map_path(), and forwards.
 * When the engine is inactive the translation is a single flag check,
 * so overhead outside containers is negligible.
 */
#include "fake_state.h"
#include "path_redirect.h"
#include "uid_spoof.h"
#include "vfs_emu.h"
#include "log.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define AC_EXPORT __attribute__((visibility("default")))

using namespace acfake;

namespace {

/* sandbox root + longest plausible guest path */
constexpr size_t kPathBufSize = 8192;

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

/*
 * Translates one absolute path argument for a hook call.
 * get() returns NULL only on buffer overflow (caller raises ENAMETOOLONG).
 */
class Translated {
public:
    explicit Translated(const char* path)
        : result_(map_path(path, buf_, sizeof(buf_))) {
        value_ = (result_ == MapResult::Mapped) ? buf_
               : (result_ == MapResult::Overflow) ? nullptr
                                                  : path;
    }

    const char* get() const { return value_; }
    bool overflowed() const { return result_ == MapResult::Overflow; }
    bool mapped() const { return result_ == MapResult::Mapped; }

private:
    MapResult result_;
    const char* value_;
    char buf_[kPathBufSize];
};

inline int fail_enametoolong() {
    errno = ENAMETOOLONG;
    return -1;
}

} // namespace

/* ------------------------------------------------------------------ */
/* open family                                                         */
/* ------------------------------------------------------------------ */

AC_EXPORT int open(const char* pathname, int flags, ...) {
    static void* s_real;
    if (enabled() && is_virtual_fs_path(pathname)) {
        int vfd = emu_open(pathname);
        if (vfd != -2) return vfd;
    }
    Translated t(pathname);
    if (t.overflowed()) return fail_enametoolong();
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        auto mode = static_cast<mode_t>(va_arg(ap, unsigned int));
        va_end(ap);
        using Fn = int (*)(const char*, int, mode_t);
        return reinterpret_cast<Fn>(next_sym(&s_real, "open"))(t.get(), flags, mode);
    }
    using Fn = int (*)(const char*, int);
    return reinterpret_cast<Fn>(next_sym(&s_real, "open"))(t.get(), flags);
}

AC_EXPORT int open64(const char* pathname, int flags, ...) {
    static void* s_real;
    Translated t(pathname);
    if (t.overflowed()) return fail_enametoolong();
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        auto mode = static_cast<mode_t>(va_arg(ap, unsigned int));
        va_end(ap);
        using Fn = int (*)(const char*, int, mode_t);
        return reinterpret_cast<Fn>(next_sym(&s_real, "open64"))(t.get(), flags, mode);
    }
    using Fn = int (*)(const char*, int);
    return reinterpret_cast<Fn>(next_sym(&s_real, "open64"))(t.get(), flags);
}

AC_EXPORT int openat(int dirfd, const char* pathname, int flags, ...) {
    static void* s_real;
    /* Absolute paths are translated; dirfd-relative ones leave the
     * pathname namespace untouched and pass through verbatim. */
    if (pathname != nullptr && pathname[0] == '/') {
        Translated t(pathname);
        if (t.overflowed()) return fail_enametoolong();
        if (flags & O_CREAT) {
            va_list ap;
            va_start(ap, flags);
            auto mode = static_cast<mode_t>(va_arg(ap, unsigned int));
            va_end(ap);
            using Fn = int (*)(int, const char*, int, mode_t);
            return reinterpret_cast<Fn>(next_sym(&s_real, "openat"))(dirfd, t.get(), flags, mode);
        }
        using Fn = int (*)(int, const char*, int);
        return reinterpret_cast<Fn>(next_sym(&s_real, "openat"))(dirfd, t.get(), flags);
    }
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        auto mode = static_cast<mode_t>(va_arg(ap, unsigned int));
        va_end(ap);
        using Fn = int (*)(int, const char*, int, mode_t);
        return reinterpret_cast<Fn>(next_sym(&s_real, "openat"))(dirfd, pathname, flags, mode);
    }
    using Fn = int (*)(int, const char*, int);
    return reinterpret_cast<Fn>(next_sym(&s_real, "openat"))(dirfd, pathname, flags);
}

AC_EXPORT int creat(const char* pathname, mode_t mode) {
    static void* s_real;
    Translated t(pathname);
    if (t.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*, mode_t);
    return reinterpret_cast<Fn>(next_sym(&s_real, "creat"))(t.get(), mode);
}

AC_EXPORT FILE* fopen(const char* path, const char* mode) {
    static void* s_real;
    if (enabled() && is_virtual_fs_path(path)) {
        int vfd = emu_open(path);
        if (vfd != -2) {
            if (vfd < 0) return nullptr; /* errno set by emu layer */
            using FdFn = FILE* (*)(int, const char*);
            return reinterpret_cast<FdFn>(next_sym(&s_real, "fdopen"))(vfd, mode);
        }
    }
    Translated t(path);
    if (t.overflowed()) {
        errno = ENAMETOOLONG;
        return nullptr;
    }
    using Fn = FILE* (*)(const char*, const char*);
    return reinterpret_cast<Fn>(next_sym(&s_real, "fopen"))(t.get(), mode);
}

AC_EXPORT FILE* fopen64(const char* path, const char* mode) {
    static void* s_real;
    Translated t(path);
    if (t.overflowed()) {
        errno = ENAMETOOLONG;
        return nullptr;
    }
    using Fn = FILE* (*)(const char*, const char*);
    return reinterpret_cast<Fn>(next_sym(&s_real, "fopen64"))(t.get(), mode);
}

/* ------------------------------------------------------------------ */
/* stat family                                                         */
/* ------------------------------------------------------------------ */

AC_EXPORT int stat(const char* path, struct stat* st) {
    static void* s_real;
    if (st != nullptr && enabled() && is_virtual_fs_path(path)) {
        int vr = emu_stat(path, st);
        if (vr != -2) return vr;
    }
    Translated t(path);
    if (t.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*, struct stat*);
    int rc = reinterpret_cast<Fn>(next_sym(&s_real, "stat"))(t.get(), st);
    if (rc == 0 && t.mapped()) acfake::fix_stat_owner(st);
    return rc;
}

AC_EXPORT int lstat(const char* path, struct stat* st) {
    static void* s_real;
    Translated t(path);
    if (t.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*, struct stat*);
    int rc = reinterpret_cast<Fn>(next_sym(&s_real, "lstat"))(t.get(), st);
    if (rc == 0 && t.mapped()) acfake::fix_stat_owner(st);
    return rc;
}

AC_EXPORT int fstatat(int dirfd, const char* path, struct stat* st, int flags) {
    static void* s_real;
    if (path != nullptr && path[0] == '/') {
        Translated t(path);
        if (t.overflowed()) return fail_enametoolong();
        using Fn = int (*)(int, const char*, struct stat*, int);
        int rc = reinterpret_cast<Fn>(next_sym(&s_real, "fstatat"))(dirfd, t.get(), st, flags);
        if (rc == 0 && t.mapped()) acfake::fix_stat_owner(st);
        return rc;
    }
    using Fn = int (*)(int, const char*, struct stat*, int);
    return reinterpret_cast<Fn>(next_sym(&s_real, "fstatat"))(dirfd, path, st, flags);
}

/* LP64 bionic exports *64 aliases taking struct stat64 (same binary
 * layout as struct stat on LP64); declared here because bionic headers
 * only expose them for LFS builds. */
extern "C" AC_EXPORT int stat64(const char* path, struct stat64* st) { return stat(path, reinterpret_cast<struct stat*>(st)); }
extern "C" AC_EXPORT int lstat64(const char* path, struct stat64* st) { return lstat(path, reinterpret_cast<struct stat*>(st)); }
extern "C" AC_EXPORT int fstatat64(int d, const char* p, struct stat64* st, int f) {
    return fstatat(d, p, reinterpret_cast<struct stat*>(st), f);
}

/* ------------------------------------------------------------------ */
/* access / readlink                                                   */
/* ------------------------------------------------------------------ */

AC_EXPORT int access(const char* path, int mode) {
    static void* s_real;
    if (enabled() && is_virtual_fs_path(path)) {
        int err = 0;
        if (emu_access(path, mode, &err)) {
            if (err != 0) { errno = err; return -1; }
            return 0;
        }
    }
    Translated t(path);
    if (t.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*, int);
    return reinterpret_cast<Fn>(next_sym(&s_real, "access"))(t.get(), mode);
}

AC_EXPORT int faccessat(int dirfd, const char* path, int mode, int flags) {
    static void* s_real;
    if (path != nullptr && path[0] == '/') {
        Translated t(path);
        if (t.overflowed()) return fail_enametoolong();
        using Fn = int (*)(int, const char*, int, int);
        return reinterpret_cast<Fn>(next_sym(&s_real, "faccessat"))(dirfd, t.get(), mode, flags);
    }
    using Fn = int (*)(int, const char*, int, int);
    return reinterpret_cast<Fn>(next_sym(&s_real, "faccessat"))(dirfd, path, mode, flags);
}

AC_EXPORT ssize_t readlink(const char* path, char* buf, size_t size) {
    static void* s_real;
    if (buf != nullptr && enabled() && is_virtual_fs_path(path)) {
        ssize_t vlen = -1;
        if (emu_readlink(path, buf, size, &vlen)) {
            if (vlen < 0) return -1;
            return vlen;
        }
    }
    Translated t(path);
    if (t.overflowed()) return static_cast<ssize_t>(fail_enametoolong());
    using Fn = ssize_t (*)(const char*, char*, size_t);
    return reinterpret_cast<Fn>(next_sym(&s_real, "readlink"))(t.get(), buf, size);
}

AC_EXPORT ssize_t readlinkat(int dirfd, const char* path, char* buf, size_t size) {
    static void* s_real;
    if (path != nullptr && path[0] == '/') {
        Translated t(path);
        if (t.overflowed()) return static_cast<ssize_t>(fail_enametoolong());
        using Fn = ssize_t (*)(int, const char*, char*, size_t);
        return reinterpret_cast<Fn>(next_sym(&s_real, "readlinkat"))(dirfd, t.get(), buf, size);
    }
    using Fn = ssize_t (*)(int, const char*, char*, size_t);
    return reinterpret_cast<Fn>(next_sym(&s_real, "readlinkat"))(dirfd, path, buf, size);
}

/* ------------------------------------------------------------------ */
/* directory iteration                                                 */
/* ------------------------------------------------------------------ */

AC_EXPORT DIR* opendir(const char* name) {
    static void* s_real;
    bool handled = false;
    if (enabled()) {
        DIR* vd = emu_opendir(name, &handled);
        if (handled) return vd;
    }
    Translated t(name);
    if (t.overflowed()) {
        errno = ENAMETOOLONG;
        return nullptr;
    }
    using Fn = DIR* (*)(const char*);
    return reinterpret_cast<Fn>(next_sym(&s_real, "opendir"))(t.get());
}

AC_EXPORT int scandir(const char* dir, struct dirent*** namelist,
                      int (*select_fn)(const struct dirent*),
                      int (*compar)(const struct dirent**, const struct dirent**)) {
    static void* s_real;
    Translated t(dir);
    if (t.overflowed()) {
        errno = ENAMETOOLONG;
        return -1;
    }
    using Fn = int (*)(const char*, struct dirent***,
                       int (*)(const struct dirent*),
                       int (*)(const struct dirent**, const struct dirent**));
    return reinterpret_cast<Fn>(next_sym(&s_real, "scandir"))(
        t.get(), namelist, select_fn, compar);
}
