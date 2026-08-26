/*
 * Guest-side frame producer (used by the fake gralloc/surfaceflinger
 * bridge). Attaches to the memfd inherited via AC_FRAME_FD and publishes
 * RGBA frames into the shared seqlock ring.
 */
#ifndef FRAME_PRODUCER_H
#define FRAME_PRODUCER_H

#include <stddef.h>
#include <stdint.h>

namespace acfake {

/* Attach to the descriptor announced through the environment. Returns
 * true on success; safe to call once from the engine constructor. */
bool frame_producer_init_from_env(void);

/* True when a shared region is attached. */
bool frame_producer_active(void);

/*
 * Publish one full frame. `pixels` must be width*height*4 bytes of RGBA.
 * Returns false when no channel is attached or the payload size is wrong.
 */
bool frame_publish(const void* pixels, size_t len);

/* Region geometry reported by the host (0 when inactive). */
uint32_t frame_width(void);
uint32_t frame_height(void);

} // namespace acfake

#endif /* FRAME_PRODUCER_H */
