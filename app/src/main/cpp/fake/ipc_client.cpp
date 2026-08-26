#include "ipc_client.h"
#include "container_common.h"
#include "fake_state.h"
#include "input_injector.h"
#include "log.h"
#include "path_redirect.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "ac_ipc_protocol.h"

namespace acfake {

namespace {

constexpr size_t kMaxPayload = 1u << 20;
constexpr useconds_t kRetryUs = 500 * 1000;

struct [[gnu::packed]] PacketHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t length;
};

volatile bool g_running = false;

bool ReadFull(int fd, void* buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, reinterpret_cast<char*>(buf) + got, len - got, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        got += static_cast<size_t>(n);
    }
    return true;
}

void Dispatch(uint16_t type, const uint8_t* data, uint32_t len) {
    switch (type) {
        case AC_IPC_INPUT_TOUCH: {
            if (len != sizeof(struct AcTouchEvent)) {
                AC_LOGW("touch packet size mismatch: %u", len);
                return;
            }
            struct AcTouchEvent ev;
            memcpy(&ev, data, sizeof(ev));
            inject_touch(&ev);
            break;
        }
        case AC_IPC_INPUT_KEY: {
            if (len != sizeof(struct AcKeyEvent)) {
                AC_LOGW("key packet size mismatch: %u", len);
                return;
            }
            struct AcKeyEvent kev;
            memcpy(&kev, data, sizeof(kev));
            inject_key(&kev);
            break;
        }
        default:
            break; /* PONG/STATE/etc: nothing to do guest-side yet */
    }
}

int ConnectHost(const char* host_sock) {
    /* Socket paths are resolved by the KERNEL, bypassing our pathname
     * hooks — map the virtual endpoint manually. */
    char mapped[8192];
    const char* target = host_sock;
    if (host_sock[0] == '/') {
        MapResult r = map_path(host_sock, mapped, sizeof(mapped));
        if (r == MapResult::Overflow) return -1;
        if (r == MapResult::Mapped) target = mapped;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(target) >= sizeof(addr.sun_path)) {
        close(fd);
        return -1;
    }
    strcpy(addr.sun_path, target);

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void AnnounceReady(int fd) {
    PacketHeader h;
    h.magic = htonl(AC_IPC_MAGIC);
    h.version = htons(AC_IPC_VERSION);
    h.type = htons(AC_IPC_GUEST_READY);
    h.length = htonl(0);
    ssize_t n = send(fd, &h, sizeof(h), MSG_NOSIGNAL);
    (void)n;
}

void* ClientThread(void* /*arg*/) {
    const char* sock = getenv(AC_ENV_IPC_SOCK);
    if (sock == nullptr || sock[0] == '\0') {
        sock = AC_IPC_DEFAULT_SOCK;
    }

    while (g_running) {
        int fd = ConnectHost(sock);
        if (fd < 0) {
            usleep(kRetryUs);
            continue;
        }
        AC_LOGD("ipc client connected to %s", sock);
        AnnounceReady(fd);

        PacketHeader h;
        for (;;) {
            if (!ReadFull(fd, &h, sizeof(h))) break;
            h.magic = ntohl(h.magic);
            h.version = ntohs(h.version);
            h.type = ntohs(h.type);
            h.length = ntohl(h.length);
            if (h.magic != AC_IPC_MAGIC || h.version != AC_IPC_VERSION ||
                h.length > kMaxPayload) {
                AC_LOGW("ipc client: bad frame");
                break;
            }

            uint8_t payload[kMaxPayload];
            if (h.length > 0 && !ReadFull(fd, payload, h.length)) break;
            Dispatch(h.type, payload, h.length);
        }
        close(fd);
    }
    return nullptr;
}

} // namespace

bool ipc_client_init_from_env(void) {
    if (!enabled()) {
        return false;
    }
    input_injector_init();

    g_running = true;
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, ClientThread, nullptr) != 0) {
        g_running = false;
        pthread_attr_destroy(&attr);
        return false;
    }
    pthread_attr_destroy(&attr);
    return true;
}

} // namespace acfake
