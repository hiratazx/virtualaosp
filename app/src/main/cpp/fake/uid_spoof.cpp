/*
 * Identity spoofing: guest processes believe they run as root (uid/gid 0)
 * while the kernel keeps enforcing the untrusted app uid of the host app.
 *
 * Query hooks return the fake identity; mutation hooks (setuid & friends)
 * report success without touching the real process credentials. passwd /
 * group database lookups are answered from a synthetic root entry so that
 * AOSP init and shell tools observe a coherent "root" environment.
 */
#include "fake_state.h"
#include "log.h"
#include "uid_spoof.h"

#include <dlfcn.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define AC_EXPORT __attribute__((visibility("default")))

using namespace acfake;

namespace acfake {

void fix_stat_owner(struct stat* st) {
    if (!enabled() || st == nullptr) {
        return;
    }
    st->st_uid = static_cast<uid_t>(fake_uid());
    st->st_gid = static_cast<gid_t>(fake_gid());
}

} // namespace acfake

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

uid_t real_uid() {
    static void* s;
    using Fn = uid_t (*)(void);
    return reinterpret_cast<Fn>(next_sym(&s, "getuid"))();
}

/* Synthetic root passwd/group entries. */
struct passwd g_passwd = {
    .pw_name = const_cast<char*>("root"),
    .pw_passwd = const_cast<char*>("*"),
    .pw_uid = 0,
    .pw_gid = 0,
    .pw_gecos = const_cast<char*>("container root"),
    .pw_dir = const_cast<char*>("/"),
    .pw_shell = const_cast<char*>("/system/bin/sh"),
};

struct group g_group = {
    .gr_name = const_cast<char*>("root"),
    .gr_passwd = const_cast<char*>("*"),
    .gr_gid = 0,
    .gr_mem = nullptr,
};

} // namespace

/* ------------------------------------------------------------------ */
/* query                                                               */
/* ------------------------------------------------------------------ */

AC_EXPORT uid_t getuid(void) {
    if (!enabled()) {
        static void* s;
        using Fn = uid_t (*)(void);
        return reinterpret_cast<Fn>(next_sym(&s, "getuid"))();
    }
    return static_cast<uid_t>(fake_uid());
}

AC_EXPORT uid_t geteuid(void) {
    if (!enabled()) {
        static void* s;
        using Fn = uid_t (*)(void);
        return reinterpret_cast<Fn>(next_sym(&s, "geteuid"))();
    }
    return static_cast<uid_t>(fake_uid());
}

AC_EXPORT gid_t getgid(void) {
    if (!enabled()) {
        static void* s;
        using Fn = gid_t (*)(void);
        return reinterpret_cast<Fn>(next_sym(&s, "getgid"))();
    }
    return static_cast<gid_t>(fake_gid());
}

AC_EXPORT gid_t getegid(void) {
    if (!enabled()) {
        static void* s;
        using Fn = gid_t (*)(void);
        return reinterpret_cast<Fn>(next_sym(&s, "getegid"))();
    }
    return static_cast<gid_t>(fake_gid());
}

AC_EXPORT int getresuid(uid_t* ruid, uid_t* euid, uid_t* suid) {
    if (!enabled()) {
        static void* s;
        using Fn = int (*)(uid_t*, uid_t*, uid_t*);
        return reinterpret_cast<Fn>(next_sym(&s, "getresuid"))(ruid, euid, suid);
    }
    if (ruid == nullptr || euid == nullptr || suid == nullptr) {
        errno = EFAULT;
        return -1;
    }
    *ruid = *euid = *suid = static_cast<uid_t>(fake_uid());
    return 0;
}

AC_EXPORT int getresgid(gid_t* rgid, gid_t* egid, gid_t* sgid) {
    if (!enabled()) {
        static void* s;
        using Fn = int (*)(gid_t*, gid_t*, gid_t*);
        return reinterpret_cast<Fn>(next_sym(&s, "getresgid"))(rgid, egid, sgid);
    }
    if (rgid == nullptr || egid == nullptr || sgid == nullptr) {
        errno = EFAULT;
        return -1;
    }
    *rgid = *egid = *sgid = static_cast<gid_t>(fake_gid());
    return 0;
}

AC_EXPORT int getgroups(int size, gid_t list[]) {
    if (!enabled()) {
        static void* s;
        using Fn = int (*)(int, gid_t*);
        return reinterpret_cast<Fn>(next_sym(&s, "getgroups"))(size, list);
    }
    if (size < 1) {
        /* POSIX: return the number of supplementary groups when list is
         * too small / NULL; the container reports exactly one. */
        errno = size < 0 ? EINVAL : 0;
        return size < 0 ? -1 : 1;
    }
    if (list == nullptr) {
        errno = EFAULT;
        return -1;
    }
    list[0] = static_cast<gid_t>(fake_gid());
    return 1;
}

AC_EXPORT struct passwd* getpwuid(uid_t uid) {
    if (!enabled() || uid != static_cast<uid_t>(fake_uid())) {
        static void* s;
        using Fn = struct passwd* (*)(uid_t);
        return reinterpret_cast<Fn>(next_sym(&s, "getpwuid"))(uid);
    }
    return &g_passwd;
}

AC_EXPORT struct passwd* getpwnam(const char* name) {
    if (!enabled() || name == nullptr || strcmp(name, "root") != 0) {
        static void* s;
        using Fn = struct passwd* (*)(const char*);
        return reinterpret_cast<Fn>(next_sym(&s, "getpwnam"))(name);
    }
    return &g_passwd;
}

AC_EXPORT struct group* getgrgid(gid_t gid) {
    if (!enabled() || gid != static_cast<gid_t>(fake_gid())) {
        static void* s;
        using Fn = struct group* (*)(gid_t);
        return reinterpret_cast<Fn>(next_sym(&s, "getgrgid"))(gid);
    }
    return &g_group;
}

AC_EXPORT struct group* getgrnam(const char* name) {
    if (!enabled() || name == nullptr || strcmp(name, "root") != 0) {
        static void* s;
        using Fn = struct group* (*)(const char*);
        return reinterpret_cast<Fn>(next_sym(&s, "getgrnam"))(name);
    }
    return &g_group;
}

/* ------------------------------------------------------------------ */
/* mutation: fake success without touching real credentials           */
/* ------------------------------------------------------------------ */

#define AC_NOOP_ID_HOOK(ret_type, name, params, args)              \
    AC_EXPORT ret_type name params {                               \
        if (!enabled()) {                                          \
            static void* ac_slot__;                                \
            using Fn = ret_type(*) params;                         \
            return reinterpret_cast<Fn>(next_sym(&ac_slot__, #name)) args; \
        }                                                          \
        return 0;                                                  \
    }

AC_NOOP_ID_HOOK(int, setuid, (uid_t uid), (uid))
AC_NOOP_ID_HOOK(int, seteuid, (uid_t euid), (euid))
AC_NOOP_ID_HOOK(int, setreuid, (uid_t ruid, uid_t euid), (ruid, euid))
AC_NOOP_ID_HOOK(int, setresuid, (uid_t r, uid_t e, uid_t s), (r, e, s))
AC_NOOP_ID_HOOK(int, setfsuid, (uid_t fsuid), (fsuid))
AC_NOOP_ID_HOOK(int, setgid, (gid_t gid), (gid))
AC_NOOP_ID_HOOK(int, setegid, (gid_t egid), (egid))
AC_NOOP_ID_HOOK(int, setregid, (gid_t rgid, gid_t egid), (rgid, egid))
AC_NOOP_ID_HOOK(int, setresgid, (gid_t r, gid_t e, gid_t s), (r, e, s))
AC_NOOP_ID_HOOK(int, setfsgid, (gid_t fsgid), (fsgid))
AC_NOOP_ID_HOOK(int, setgroups, (size_t size, const gid_t* list), (size, list))

AC_EXPORT int initgroups(const char* user, gid_t group) {
    (void)user;
    (void)group;
    return enabled() ? 0
                     : [] {
                           static void* s;
                           using Fn = int (*)(const char*, gid_t);
                           return reinterpret_cast<Fn>(next_sym(&s, "initgroups"));
                       }()(user, group);
}
