#include "seccomp_filter.h"
#include "container_common.h"
#include "log.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

using namespace acfake;

namespace {

/* Per-architecture syscall numbers (asm-generic for arm64, native x86_64).
 * 0xFFFFFFFF marks syscalls absent from the architecture. */
#if defined(__aarch64__)
[[maybe_unused]] constexpr unsigned long kAudArch = AUDIT_ARCH_AARCH64;
constexpr unsigned long kNrMount = 40;
constexpr unsigned long kNrUmount2 = 39;
constexpr unsigned long kNrChroot = 51;
constexpr unsigned long kNrMknod = 0xFFFFFFFFUL; /* not present on arm64 */
constexpr unsigned long kNrPivotRoot = 41;
constexpr unsigned long kNrInitModule = 105;
constexpr unsigned long kNrDeleteModule = 106;
constexpr unsigned long kNrKexecLoad = 104;
constexpr unsigned long kNrSwapon = 224;
constexpr unsigned long kNrSwapoff = 225;
constexpr unsigned long kNrReboot = 142;
constexpr unsigned long kNrKeyctl = 219;
constexpr unsigned long kNrBpf = 280;
constexpr unsigned long kNrOpenByHandleAt = 265;
#elif defined(__x86_64__)
constexpr unsigned long kAudArch = AUDIT_ARCH_X86_64;
constexpr unsigned long kNrMount = 165;
constexpr unsigned long kNrUmount2 = 166;
constexpr unsigned long kNrChroot = 161;
constexpr unsigned long kNrMknod = 133;
constexpr unsigned long kNrPivotRoot = 155;
constexpr unsigned long kNrInitModule = 175;
constexpr unsigned long kNrDeleteModule = 176;
constexpr unsigned long kNrKexecLoad = 246;
constexpr unsigned long kNrSwapon = 167;
constexpr unsigned long kNrSwapoff = 168;
constexpr unsigned long kNrReboot = 169;
constexpr unsigned long kNrKeyctl = 250;
constexpr unsigned long kNrBpf = 321;
constexpr unsigned long kNrOpenByHandleAt = 304;
#else
#error "unsupported ABI for seccomp filter"
#endif

const unsigned long kDenied[] = {
    kNrMount,     kNrUmount2,        kNrChroot,
    kNrPivotRoot, kNrInitModule,     kNrDeleteModule,
    kNrKexecLoad, kNrSwapon,         kNrSwapoff,
    kNrReboot,    kNrKeyctl,         kNrBpf,
    kNrOpenByHandleAt,
};

} // namespace

namespace acfake {

bool install_seccomp_filter() {
    /*
     * Program layout:
     *   [0] LD  arch
     *   [1] JEQ kAudArch  -> mismatch jumps forward to RET ALLOW
     *   [2] LD  nr
     *   [3..] per denied syscall:
     *         JEQ nr -> miss skips the following RET
     *         RET ERRNO|EPERM
     *   [N] RET ALLOW
     */
    constexpr size_t kMaxDenied = sizeof(kDenied) / sizeof(kDenied[0]);
    struct sock_filter prog[3 + kMaxDenied * 2 + 1];

    size_t n_denied = 0;
    for (unsigned long nr : kDenied) {
        if (nr != 0xFFFFFFFFUL) ++n_denied;
    }
    const size_t total = 3 + n_denied * 2 + 1;

    size_t i = 0;
    prog[i++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                                             offsetof(struct seccomp_data, arch));
    /* at idx1, next=idx2; target idx(total-1); offset = (total-1)-2 */
    prog[i++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, kAudArch,
                                             0, static_cast<unsigned char>(total - 3));
    prog[i++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                                             offsetof(struct seccomp_data, nr));

    for (unsigned long nr : kDenied) {
        if (nr == 0xFFFFFFFFUL) continue;
        prog[i++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                                 static_cast<__u32>(nr), 0, 1);
        prog[i++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K,
                                                 SECCOMP_RET_ERRNO |
                                                     (EPERM & SECCOMP_RET_DATA));
    }

    prog[i++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    if (i != total) {
        AC_LOGE("seccomp program size mismatch (%zu != %zu)", i, total);
        return false;
    }

    struct sock_fprog fprog = {
        .len = static_cast<unsigned short>(total),
        .filter = prog,
    };

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        AC_LOGW("PR_SET_NO_NEW_PRIVS failed: %s", strerror(errno));
        return false;
    }
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &fprog, 0, 0) != 0) {
        AC_LOGW("PR_SET_SECCOMP failed: %s", strerror(errno));
        return false;
    }
    AC_LOGI("seccomp denylist installed (%zu syscalls)", n_denied);
    return true;
}

} // namespace acfake
