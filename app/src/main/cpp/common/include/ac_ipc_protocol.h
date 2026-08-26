/*
 * Wire-level IPC contract shared between libcontainer_core (host) and
 * libfake (guest). Kept header-only and dependency-free so both
 * libraries agree without linking each other.
 */
#ifndef AC_IPC_PROTOCOL_H
#define AC_IPC_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AC_IPC_MAGIC 0x41434950u /* "ACIP" */
#define AC_IPC_VERSION 1

/* Packet types (extensible; frame subscription added in later phases). */
enum {
    AC_IPC_PING = 0x01,
    AC_IPC_PONG = 0x02,
    AC_IPC_STATE = 0x03,
    AC_IPC_GUEST_READY = 0x04,
    AC_IPC_SHUTDOWN = 0x05,
    AC_IPC_INPUT_TOUCH = 0x10,
    AC_IPC_INPUT_KEY = 0x11,
};

/* Normalized touch event. Coordinates use 16.16 fixed point in [0,65535]
 * so the guest maps them onto its own display geometry. */
struct AcTouchEvent {
    uint32_t action;      /* one of AC_TOUCH_ACTION_* */
    uint32_t pointer_id;
    uint32_t x_fixed;     /* 16.16 */
    uint32_t y_fixed;     /* 16.16 */
    uint32_t pressure;    /* 0..65535 */
};

/* Key event record; action 0 = down, 1 = up. */
struct AcKeyEvent {
    uint32_t keycode;
    uint32_t action;
};

/* Subset of MotionEvent actions the bridge forwards. */
enum {
    AC_TOUCH_ACTION_DOWN = 0,
    AC_TOUCH_ACTION_MOVE = 1,
    AC_TOUCH_ACTION_UP = 2,
    AC_TOUCH_ACTION_CANCEL = 3,
    AC_TOUCH_ACTION_POINTER_DOWN = 4,
    AC_TOUCH_ACTION_POINTER_UP = 5,
};

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AC_IPC_PROTOCOL_H */
