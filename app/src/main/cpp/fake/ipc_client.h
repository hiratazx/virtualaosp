/*
 * Guest-side IPC client: connects back to the host daemon through the
 * redirected /.host.sock endpoint, keeps the link alive with automatic
 * reconnects, and dispatches inbound packets (touch input today, frame
 * control in later phases).
 */
#ifndef IPC_CLIENT_H
#define IPC_CLIENT_H

namespace acfake {

/* Spawns the client thread when running inside a container. */
bool ipc_client_init_from_env(void);

} // namespace acfake

#endif /* IPC_CLIENT_H */
