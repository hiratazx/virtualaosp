/*
 * Zero-copy frame transport contract shared by libcontainer_core (host
 * consumer side) and libfake (guest producer side).
 *
 * Transport: a memfd-backed region created by the host before the guest
 * starts. The raw descriptor is inherited across fork/exec and announced
 * to the guest through AC_FRAME_FD, so no SCM_RIGHTS dance is required.
 *
 * Synchronization: classic seqlock. The producer writes pixel bytes into
 * a slot, then publishes with a release-store of an increasing sequence
 * number. Consumers validate a snapshot by re-reading the sequence after
 * copying (acquire loads); mismatch => retry next vsync.
 */
#ifndef FRAME_PROTOCOL_H
#define FRAME_PROTOCOL_H

#include <stdint.h>

#define AC_FRAME_MAGIC 0x41435246u /* "ACRF" */
#define AC_FRAME_VERSION 1

/* Pixel formats (subset of AHardwareBuffer formats we support). */
#define AC_FRAME_FORMAT_RGBA8888 1

/* Environment variable carrying the inherited memfd number. */
#define AC_ENV_FRAME_FD "AC_FRAME_FD"

#ifdef __cplusplus
extern "C" {
#endif

struct AcFrameRegionHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t slot_count;
    uint64_t slot_stride; /* bytes from slot base to next slot base */
};

struct AcFrameSlotMeta {
    /* Published sequence; 0 means "never written". Odd values are not
     * used (we always store even, monotonically increasing numbers). */
    volatile uint64_t sequence;
    uint32_t bytes_used;
    uint32_t flags;
};

static inline size_t ac_frame_slot_size(uint32_t w, uint32_t h, uint32_t fmt) {
    size_t bpp = (fmt == AC_FRAME_FORMAT_RGBA8888) ? 4 : 4;
    return sizeof(struct AcFrameSlotMeta) + (size_t)w * h * bpp;
}

static inline size_t ac_frame_region_size(uint32_t w, uint32_t h,
                                          uint32_t fmt, uint32_t slots) {
    return sizeof(struct AcFrameRegionHeader) +
           (size_t)slots * ac_frame_slot_size(w, h, fmt);
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FRAME_PROTOCOL_H */
