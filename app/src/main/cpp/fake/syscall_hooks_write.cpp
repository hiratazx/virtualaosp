/*
 * Dynamic linker interposition of libc pathname WRITE/METADATA operations
 * and the exec family. Same translation discipline as syscall_hooks_read.cpp.
 */
#include "fake_state.h"
#include "path_redirect.h"
#include "log.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define AC_EXPORT __attribute__((visibility("default")))

using namespace acfake;

namespace {

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

private:
    MapResult result_;
    const char* value_;
    char buf_[kPathBufSize];
};

inline int fail_enametoolong() {
    errno = ENAMETOOLONG;
    return -1;
}

/* Helper for at()-variants: translate only absolute path components. */
const char* maybe_map(const char* path, char* buf, size_t bufsz,
                      const char** out, bool* overflow) {
    if (path != nullptr && path[0] == '/') {
        MapResult r = map_path(path, buf, bufsz);
        if (r == MapResult::Overflow) {
            *overflow = true;
            return path;
        }
        if (r == MapResult::Mapped) {
            *out = buf;
            return *out;
        }
    }
    *out = path;
    return *out;
}

} // namespace

/* ------------------------------------------------------------------ */
/* mutation                                                            */
/* ------------------------------------------------------------------ */

AC_EXPORT int mkdir(const char* path, mode_t mode) {
    static void* s_real;
    Translated t(path);
    if (t.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*, mode_t);
    return reinterpret_cast<Fn>(next_sym(&s_real, "mkdir"))(t.get(), mode);
}

AC_EXPORT int mkdirat(int dirfd, const char* path, mode_t mode) {
    static void* s_real;
    char buf[kPathBufSize];
    const char* mp = nullptr;
    bool ovf = false;
    maybe_map(path, buf, sizeof(buf), &mp, &ovf);
    if (ovf) return fail_enametoolong();
    using Fn = int (*)(int, const char*, mode_t);
    return reinterpret_cast<Fn>(next_sym(&s_real, "mkdirat"))(dirfd, mp, mode);
}

AC_EXPORT int rmdir(const char* path) {
    static void* s_real;
    Translated t(path);
    if (t.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*);
    return reinterpret_cast<Fn>(next_sym(&s_real, "rmdir"))(t.get());
}

AC_EXPORT int unlink(const char* path) {
    static void* s_real;
    Translated t(path);
    if (t.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*);
    return reinterpret_cast<Fn>(next_sym(&s_real, "unlink"))(t.get());
}

AC_EXPORT int unlinkat(int dirfd, const char* path, int flags) {
    static void* s_real;
    char buf[kPathBufSize];
    const char* mp = nullptr;
    bool ovf = false;
    maybe_map(path, buf, sizeof(buf), &mp, &ovf);
    if (ovf) return fail_enametoolong();
    using Fn = int (*)(int, const char*, int);
    return reinterpret_cast<Fn>(next_sym(&s_real, "unlinkat"))(dirfd, mp, flags);
}

AC_EXPORT int rename(const char* oldp, const char* newp) {
    static void* s_real;
    Translated to(oldp);
    Translated tn(newp);
    if (to.overflowed() || tn.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*, const char*);
    return reinterpret_cast<Fn>(next_sym(&s_real, "rename"))(to.get(), tn.get());
}

AC_EXPORT int renameat(int olddirfd, const char* oldp, int newdirfd, const char* newp) {
    static void* s_real;
    char buf1[kPathBufSize], buf2[kPathBufSize];
    const char *mo = nullptr, *mn = nullptr;
    bool o1 = false, o2 = false;
    maybe_map(oldp, buf1, sizeof(buf1), &mo, &o1);
    maybe_map(newp, buf2, sizeof(buf2), &mn, &o2);
    if (o1 || o2) return fail_enametoolong();
    using Fn = int (*)(int, const char*, int, const char*);
    return reinterpret_cast<Fn>(next_sym(&s_real, "renameat"))(olddirfd, mo, newdirfd, mn);
}

AC_EXPORT int chmod(const char* path, mode_t mode) {
    static void* s_real;
    Translated t(path);
    if (t.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*, mode_t);
    return reinterpret_cast<Fn>(next_sym(&s_real, "chmod"))(t.get(), mode);
}

AC_EXPORT int fchmodat(int dirfd, const char* path, mode_t mode, int flags) {
    static void* s_real;
    char buf[kPathBufSize];
    const char* mp = nullptr;
    bool ovf = false;
    maybe_map(path, buf, sizeof(buf), &mp, &ovf);
    if (ovf) return fail_enametoolong();
    using Fn = int (*)(int, const char*, mode_t, int);
    return reinterpret_cast<Fn>(next_sym(&s_real, "fchmodat"))(dirfd, mp, mode, flags);
}

AC_EXPORT int chown(const char* path, uid_t uid, gid_t gid) {
    static void* s_real;
    Translated t(path);
    if (t.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*, uid_t, gid_t);
    return reinterpret_cast<Fn>(next_sym(&s_real, "chown"))(t.get(), uid, gid);
}

AC_EXPORT int lchown(const char* path, uid_t uid, gid_t gid) {
    static void* s_real;
    Translated t(path);
    if (t.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*, uid_t, gid_t);
    return reinterpret_cast<Fn>(next_sym(&s_real, "lchown"))(t.get(), uid, gid);
}

AC_EXPORT int fchownat(int dirfd, const char* path, uid_t uid, gid_t gid, int flags) {
    static void* s_real;
    char buf[kPathBufSize];
    const char* mp = nullptr;
    bool ovf = false;
    maybe_map(path, buf, sizeof(buf), &mp, &ovf);
    if (ovf) return fail_enametoolong();
    using Fn = int (*)(int, const char*, uid_t, gid_t, int);
    return reinterpret_cast<Fn>(next_sym(&s_real, "fchownat"))(dirfd, mp, uid, gid, flags);
}

AC_EXPORT int truncate(const char* path, off_t length) {
    static void* s_real;
    Translated t(path);
    if (t.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*, off_t);
    return reinterpret_cast<Fn>(next_sym(&s_real, "truncate"))(t.get(), length);
}

AC_EXPORT int utimes(const char* path, const struct timeval times[2]) {
    static void* s_real;
    Translated t(path);
    if (t.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*, const struct timeval[2]);
    return reinterpret_cast<Fn>(next_sym(&s_real, "utimes"))(t.get(), times);
}

AC_EXPORT int utimensat(int dirfd, const char* path, const struct timespec times[2],
                        int flags) {
    static void* s_real;
    char buf[kPathBufSize];
    const char* mp = nullptr;
    bool ovf = false;
    maybe_map(path, buf, sizeof(buf), &mp, &ovf);
    if (ovf) return fail_enametoolong();
    using Fn = int (*)(int, const char*, const struct timespec[2], int);
    return reinterpret_cast<Fn>(next_sym(&s_real, "utimensat"))(dirfd, mp, times, flags);
}

AC_EXPORT int symlink(const char* target, const char* linkpath) {
    static void* s_real;
    /* Target stays verbatim: it is guest-namespace content stored inside
     * the sandbox file; only the link location is translated. */
    Translated t(linkpath);
    if (t.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*, const char*);
    return reinterpret_cast<Fn>(next_sym(&s_real, "symlink"))(target, t.get());
}

AC_EXPORT int link(const char* oldp, const char* newp) {
    static void* s_real;
    Translated to(oldp);
    Translated tn(newp);
    if (to.overflowed() || tn.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*, const char*);
    return reinterpret_cast<Fn>(next_sym(&s_real, "link"))(to.get(), tn.get());
}

AC_EXPORT int mkfifo(const char* path, mode_t mode) {
    static void* s_real;
    Translated t(path);
    if (t.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*, mode_t);
    return reinterpret_cast<Fn>(next_sym(&s_real, "mkfifo"))(t.get(), mode);
}

AC_EXPORT int chdir(const char* path) {
    static void* s_real;
    Translated t(path);
    if (t.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*);
    return reinterpret_cast<Fn>(next_sym(&s_real, "chdir"))(t.get());
}

/* ------------------------------------------------------------------ */
/* exec family                                                         */
/* ------------------------------------------------------------------ */

/*
 * execve is the single choke point: bionic's execv/execvp/execvpe all
 * funnel through it. PATH search performed by execvp already benefits
 * from the hooked access()/stat(). LD_PRELOAD and AC_* variables persist
 * because they live in envp, which callers pass through unchanged.
 */
AC_EXPORT int execve(const char* path, char* const argv[], char* const envp[]) {
    static void* s_real;
    Translated t(path);
    if (t.overflowed()) return fail_enametoolong();
    using Fn = int (*)(const char*, char* const[], char* const[]);
    return reinterpret_cast<Fn>(next_sym(&s_real, "execve"))(t.get(), argv, envp);
}
