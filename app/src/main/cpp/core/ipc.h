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

#include "ac_ipc_protocol.h"

namespace accore {

constexpr uint32_t kIpcMagic = AC_IPC_MAGIC;
constexpr uint16_t kIpcVersion = AC_IPC_VERSION;

/* Packet types come from the shared wire contract. */
constexpr uint16_t kIpcPing = AC_IPC_PING;
constexpr uint16_t kIpcPong = AC_IPC_PONG;
constexpr uint16_t kIpcState = AC_IPC_STATE;
constexpr uint16_t kIpcGuestReady = AC_IPC_GUEST_READY;
constexpr uint16_t kIpcShutdown = AC_IPC_SHUTDOWN;
constexpr uint16_t kIpcInputTouch = AC_IPC_INPUT_TOUCH;

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

/* Process-wide server instance shared by subsystems (JNI glue,
 * input consumer). Created lazily on first Start(). */
IpcServer& GlobalIpcServer();

} // namespace accore

#endif /* IPC_H */
