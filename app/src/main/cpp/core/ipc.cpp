#include "ipc.h"
#include "log.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace accore {

namespace {

struct [[gnu::packed]] PacketHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t length;
};

constexpr size_t kMaxPayload = 1u << 20; /* 1 MiB cap */

bool WriteAll(int fd, const uint8_t* buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::send(fd, buf + off, len - off, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

} // namespace

class IpcServer::Impl {
public:
    accore::IpcServer* owner = nullptr;
    std::mutex clients_mu;
    std::vector<int> clients;

    bool running = false;
    int listen_fd = -1;
    std::string path;
    std::shared_ptr<IpcMessageHandler> handler;
    std::thread accept_thread;
    std::vector<std::thread> client_threads;

    void SpawnClientThread(int fd);
};

IpcServer::~IpcServer() {
    Stop();
}

int IpcServer::Start(const std::string& sock_path,
                     std::shared_ptr<IpcMessageHandler> msg_handler) {
    if (impl_ != nullptr) return -EALREADY;

    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -errno;

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (sock_path.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        return -ENAMETOOLONG;
    }
    strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);

    ::unlink(sock_path.c_str());
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        int e = errno;
        ::close(fd);
        return -e;
    }
    /* Both endpoints run as the same app uid; permissive mode bits are
     * harmless and let guest-side helpers connect without extra setup. */
    ::chmod(sock_path.c_str(), 0666);
    if (::listen(fd, 8) != 0) {
        int e = errno;
        ::close(fd);
        ::unlink(sock_path.c_str());
        return -e;
    }

    impl_ = new Impl();
    impl_->owner = this;
    impl_->listen_fd = fd;
    impl_->path = sock_path;
    impl_->handler = std::move(msg_handler);
    impl_->running = true;
    listen_fd_ = fd;
    path_ = sock_path;

    impl_->accept_thread = std::thread([this] { AcceptLoop(); });
    AC_LOGI("ipc listening on %s", sock_path.c_str());
    return 0;
}

void IpcServer::Stop() {
    Impl* impl = impl_;
    if (impl == nullptr) return;
    impl_ = nullptr;

    impl->running = false;
    ::shutdown(impl->listen_fd, SHUT_RDWR);
    ::close(impl->listen_fd);
    if (impl->accept_thread.joinable()) {
        impl->accept_thread.join();
    }

    {
        std::lock_guard<std::mutex> lock(impl->clients_mu);
        for (int c : impl->clients) {
            ::shutdown(c, SHUT_RDWR);
            ::close(c);
        }
        impl->clients.clear();
    }

    for (auto& t : impl->client_threads) {
        if (t.joinable()) t.join();
    }

    ::unlink(impl->path.c_str());
    delete impl;
}

void IpcServer::AcceptLoop() {
    Impl* impl = impl_;
    while (impl->running) {
        int c = ::accept4(impl->listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (c < 0) {
            if (errno == EINTR) continue;
            if (!impl->running || errno == EBADF || errno == EINVAL) break;
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(impl->clients_mu);
            impl->clients.push_back(c);
        }
        impl->SpawnClientThread(c);
        AC_LOGD("ipc client connected fd=%d", c);
    }
}

void IpcServer::Impl::SpawnClientThread(int fd) {
    IpcServer* srv = owner;
    client_threads.emplace_back([srv, fd] { srv->ClientLoop(fd); });
}

void IpcServer::DropClient(int fd) {
    Impl* impl = impl_;
    if (impl == nullptr) return;
    ::close(fd);
    std::lock_guard<std::mutex> lock(impl->clients_mu);
    for (size_t i = 0; i < impl->clients.size(); ++i) {
        if (impl->clients[i] == fd) {
            impl->clients[i] = impl->clients.back();
            impl->clients.pop_back();
            break;
        }
    }
}

void IpcServer::ClientLoop(int fd) {
    uint8_t header[sizeof(PacketHeader)];

    for (;;) {
        /* Read one full frame. */
        size_t got = 0;
        while (got < sizeof(header)) {
            ssize_t n = ::recv(fd, header + got, sizeof(header) - got, 0);
            if (n <= 0) goto disconnect;
            got += static_cast<size_t>(n);
        }

        PacketHeader h;
        memcpy(&h, header, sizeof(h));
        h.magic = ntohl(h.magic);
        h.version = ntohs(h.version);
        h.type = ntohs(h.type);
        h.length = ntohl(h.length);

        if (h.magic != kIpcMagic || h.version != kIpcVersion ||
            h.length > kMaxPayload) {
            AC_LOGW("ipc bad frame magic=0x%x ver=%u len=%u", h.magic,
                    h.version, h.length);
            goto disconnect;
        }

        {
            std::vector<uint8_t> payload(h.length);
            size_t got2 = 0;
            while (got2 < h.length) {
                ssize_t n = ::recv(fd, payload.data() + got2, h.length - got2, 0);
                if (n <= 0) goto disconnect;
                got2 += static_cast<size_t>(n);
            }

            if (h.type == kIpcPing) {
                Send(fd, kIpcPong, payload.data(), h.length);
                continue;
            }

            auto* impl = impl_;
            if (impl != nullptr && impl->handler) {
                impl->handler->OnMessage(fd, h.type, payload.data(), h.length);
            }
        }
    }
disconnect:
    DropClient(fd);
}

bool IpcServer::Send(int client_fd, uint16_t type, const uint8_t* data,
                     uint32_t len) {
    if (len > kMaxPayload) return false;

    PacketHeader h;
    h.magic = htonl(kIpcMagic);
    h.version = htons(kIpcVersion);
    h.type = htons(type);
    h.length = htonl(len);

    uint8_t frame[sizeof(PacketHeader)];
    memcpy(frame, &h, sizeof(h));

    /* Header and small payloads go out together to reduce syscalls. */
    std::vector<uint8_t> buffer;
    const uint8_t* parts[2] = {frame, data};
    size_t lens[2] = {sizeof(frame), len};
    size_t total = lens[0] + lens[1];
    if (len <= 512) {
        buffer.resize(total);
        memcpy(buffer.data(), parts[0], lens[0]);
        if (len > 0) memcpy(buffer.data() + lens[0], parts[1], lens[1]);
        return WriteAll(client_fd, buffer.data(), total);
    }
    if (!WriteAll(client_fd, frame, sizeof(frame))) return false;
    return WriteAll(client_fd, data, len);
}

size_t IpcServer::Broadcast(uint16_t type, const uint8_t* data, uint32_t len) {
    Impl* impl = impl_;
    if (impl == nullptr) return 0;
    size_t delivered = 0;
    std::vector<int> snapshot;
    {
        std::lock_guard<std::mutex> lock(impl->clients_mu);
        snapshot = impl->clients;
    }
    for (int c : snapshot) {
        if (Send(c, type, data, len)) ++delivered;
    }
    return delivered;
}

IpcServer& GlobalIpcServer() {
    static IpcServer instance;
    return instance;
}

size_t IpcServer::client_count() const {
    Impl* impl = impl_;
    if (impl == nullptr) return 0;
    std::lock_guard<std::mutex> lock(impl->clients_mu);
    return impl->clients.size();
}

} // namespace accore
