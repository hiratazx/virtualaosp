#include "real_libc.h"
#include "log.h"

#include <dirent.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>

namespace acfake {

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

} // namespace

/* Extra register args are ignored by callees reading fewer parameters on
 * both AAPCS64 and SysV-x86_64, so always passing `mode` is safe. */
int real_open(const char* path, int flags, mode_t mode) {
    static void* s;
    using Fn = int (*)(const char*, int, mode_t);
    return reinterpret_cast<Fn>(next_sym(&s, "open"))(path, flags, mode);
}

int real_close(int fd) {
    static void* s;
    using Fn = int (*)(int);
    return reinterpret_cast<Fn>(next_sym(&s, "close"))(fd);
}

ssize_t real_read(int fd, void* buf, size_t count) {
    static void* s;
    using Fn = ssize_t (*)(int, void*, size_t);
    return reinterpret_cast<Fn>(next_sym(&s, "read"))(fd, buf, count);
}

off_t real_lseek(int fd, off_t offset, int whence) {
    static void* s;
    using Fn = off_t (*)(int, off_t, int);
    return reinterpret_cast<Fn>(next_sym(&s, "lseek"))(fd, offset, whence);
}

ssize_t real_readlink(const char* path, char* buf, size_t size) {
    static void* s;
    using Fn = ssize_t (*)(const char*, char*, size_t);
    return reinterpret_cast<Fn>(next_sym(&s, "readlink"))(path, buf, size);
}

int real_fstat(int fd, struct stat* st) {
    static void* s;
    using Fn = int (*)(int, struct stat*);
    return reinterpret_cast<Fn>(next_sym(&s, "fstat"))(fd, st);
}

FILE* real_fdopen(int fd, const char* mode) {
    static void* s;
    using Fn = FILE* (*)(int, const char*);
    return reinterpret_cast<Fn>(next_sym(&s, "fdopen"))(fd, mode);
}

DIR* real_opendir(const char* path) {
    static void* s;
    using Fn = DIR* (*)(const char*);
    return reinterpret_cast<Fn>(next_sym(&s, "opendir"))(path);
}

pid_t real_getpid() {
    static void* s;
    using Fn = pid_t (*)(void);
    return reinterpret_cast<Fn>(next_sym(&s, "getpid"))();
}

} // namespace acfake
