#include "input_injector.h"
#include "container_common.h"
#include "fake_state.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

namespace acfake {

namespace {

int g_spool_fd = -1;
pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

} // namespace

bool input_injector_init(void) {
    if (!enabled()) {
        return false;
    }
    /* Our own open() hook translates the virtual /dev path into the
     * sandbox-backed location. */
    int fd = open(AC_INPUT_SPOOL, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) {
        AC_LOGW("input spool %s unavailable: %s", AC_INPUT_SPOOL, strerror(errno));
        return false;
    }
    __atomic_store_n(&g_spool_fd, fd, __ATOMIC_RELEASE);
    AC_LOGI("input injector ready: %s", AC_INPUT_SPOOL);
    return true;
}

bool inject_touch(const struct AcTouchEvent* ev) {
    if (ev == nullptr) return false;
    const int fd = __atomic_load_n(&g_spool_fd, __ATOMIC_ACQUIRE);
    if (fd < 0) return false;

    /* Length-prefixed record: tolerant to future struct growth. */
    const uint32_t len = static_cast<uint32_t>(sizeof(*ev));
    struct iovec iov[2];
    iov[0].iov_base = const_cast<void*>(static_cast<const void*>(&len));
    iov[0].iov_len = sizeof(len);
    iov[1].iov_base = const_cast<void*>(static_cast<const void*>(ev));
    iov[1].iov_len = sizeof(*ev);

    pthread_mutex_lock(&g_mu);
    ssize_t n = writev(fd, iov, 2);
    pthread_mutex_unlock(&g_mu);
    return n == static_cast<ssize_t>(sizeof(len) + sizeof(*ev));
}

} // namespace acfake
