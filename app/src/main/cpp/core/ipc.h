/*
 * Resilient UNIX domain socket IPC between the host coordinator and the
 * guest runtime.
 *
 * Transport: a listening socket materialized INSIDE the sandbox at
 * <rootfs>/.host.sock. Because libfake redirects guest path lookups into
 * the sandbox, guests simply connect("/.host.sock") and land here — no
 * namespace tricks required.
 *
 * Framing: little-endian fixed header + opaque payload. PING is answered
 * automatically by the server as a liveness handshake.
 */
#ifndef IPC_H
#define IPC_H

#include <cstdint>
#include <memory>
#include <string>

namespace accore {

constexpr uint32_t kIpcMagic = 0x41434950u; /* "ACIP" */
constexpr uint16_t kIpcVersion = 1;

/* Packet types (extensible; input/frame types added in later phases). */
enum IpcType : uint16_t {
    kIpcPing = 0x01,
    kIpcPong = 0x02,
    kIpcState = 0x03,
    kIpcGuestReady = 0x04,
    kIpcShutdown = 0x05,
};

class IpcMessageHandler {
public:
    virtual ~IpcMessageHandler() = default;
    /* Called on the client's reader thread. Payload may be empty. */
    virtual void OnMessage(int client_fd, uint16_t type,
                           const uint8_t* data, uint32_t len) = 0;
};

class IpcServer {
public:
    ~IpcServer();

    /* Binds <path>, spawns accept + client threads. Fails -errno style
     * when the endpoint cannot be created. */
    int Start(const std::string& path,
              std::shared_ptr<IpcMessageHandler> handler);

    /* Joins all threads and removes the socket node. */
    void Stop();

    bool Send(int client_fd, uint16_t type, const uint8_t* data, uint32_t len);
    size_t Broadcast(uint16_t type, const uint8_t* data, uint32_t len);
    size_t client_count() const;

private:
    friend class Impl;

    void AcceptLoop();
    void ClientLoop(int fd);
    void DropClient(int fd);

    int listen_fd_ = -1;
    std::string path_;
    std::shared_ptr<IpcMessageHandler> handler_;
    class Impl;
    Impl* impl_ = nullptr;
};

} // namespace accore

#endif /* IPC_H */
