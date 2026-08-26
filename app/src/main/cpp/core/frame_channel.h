/*
 * Host-side frame channel: creates the shared region, exposes a
 * vsync-paced consumer that copies the latest stable frame out for
 * presentation (Phase 3.2 feeds it to ANativeWindow).
 */
#ifndef FRAME_CHANNEL_H
#define FRAME_CHANNEL_H

#include <cstdint>
#include <memory>

#include "frame_protocol.h"

namespace accore {

class FrameChannelHost {
public:
    ~FrameChannelHost();

    /* Creates the memfd + mapping. Returns -errno on failure. */
    static std::unique_ptr<FrameChannelHost> Create(uint32_t width,
                                                    uint32_t height,
                                                    uint32_t slot_count);

    /* Raw descriptor to hand down into the guest environment. */
    int fd() const { return fd_; }
    const AcFrameRegionHeader* header() const { return header_; }

    struct Snapshot {
        uint64_t sequence = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    /*
     * Copies the newest seqlock-stable frame into `dst` (expected
     * width*height*4 bytes, RGBA8888). Returns false when no frame has
     * been published yet or no stable snapshot was possible this pass
     * (caller simply presents the previous buffer).
     */
    bool ReadLatest(uint8_t* dst, size_t dst_len, Snapshot* out);

private:
    FrameChannelHost() = default;

    int fd_ = -1;
    uint8_t* region_ = nullptr;
    size_t region_size_ = 0;
    AcFrameRegionHeader* header_ = nullptr;
};

} // namespace accore

#endif /* FRAME_CHANNEL_H */
