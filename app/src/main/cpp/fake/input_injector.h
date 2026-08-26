/*
 * Guest-side input injection sink.
 *
 * The host forwards normalized touch packets over IPC; this module lands
 * them into a spool file inside the sandbox (<rootfs>/dev/ac_input).
 * The guest rootfs ships an `ac_input` helper that tails the spool and
 * feeds the events into the Android input framework with spoofed-root
 * privileges (uinput access is itself routed through the redirection +
 * emulation layers).
 *
 * Records are length-prefixed little-endian structs so partial writes
 * never corrupt the stream.
 */
#ifndef INPUT_INJECTOR_H
#define INPUT_INJECTOR_H

#include <stdint.h>

#include "ac_ipc_protocol.h"

namespace acfake {

bool input_injector_init(void);

/* Append one event to the spool. Thread-safe. */
bool inject_touch(const struct AcTouchEvent* ev);

} // namespace acfake

#endif /* INPUT_INJECTOR_H */
