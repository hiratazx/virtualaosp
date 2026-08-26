#include "frame_channel.h"
#include "log.h"

#include <cerrno>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace accore {

namespace {

constexpr size_t kPageSize = 4096;

AcFrameSlotMeta* SlotMeta(uint8_t* base, const AcFrameRegionHeader* h, uint32_t i) {
    return reinterpret_cast<AcFrameSlotMeta*>(
        base + sizeof(AcFrameRegionHeader) + (size_t)i * h->slot_stride);
}

uint8_t* SlotPixels(uint8_t* base, const AcFrameRegionHeader* h, uint32_t i) {
    return reinterpret_cast<uint8_t*>(SlotMeta(base, h, i)) + sizeof(AcFrameSlotMeta);
}

} // namespace

FrameChannelHost::~FrameChannelHost() {
    if (region_ != nullptr) {
        munmap(region_, region_size_);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

std::unique_ptr<FrameChannelHost> FrameChannelHost::Create(uint32_t width,
                                                           uint32_t height,
                                                           uint32_t slot_count) {
    if (width == 0 || height == 0 || slot_count < 2 || slot_count > 16) {
        return nullptr;
    }

    auto channel = std::unique_ptr<FrameChannelHost>(new FrameChannelHost());
    channel->fd_ = memfd_create("ac_frames", MFD_CLOEXEC);
    if (channel->fd_ < 0) {
        AC_LOGE("memfd_create failed: %s", strerror(errno));
        return nullptr;
    }

    const size_t total = ac_frame_region_size(width, height,
                                              AC_FRAME_FORMAT_RGBA8888, slot_count);
    if (ftruncate(channel->fd_, static_cast<off_t>(total)) != 0) {
        AC_LOGE("ftruncate failed: %s", strerror(errno));
        return nullptr;
    }

    channel->region_size_ = ((total + kPageSize - 1) / kPageSize) * kPageSize;
    void* map = mmap(nullptr, channel->region_size_,
                     PROT_READ | PROT_WRITE, MAP_SHARED, channel->fd_, 0);
    if (map == MAP_FAILED) {
        AC_LOGE("mmap failed: %s", strerror(errno));
        return nullptr;
    }
    channel->region_ = static_cast<uint8_t*>(map);

    auto* h = reinterpret_cast<AcFrameRegionHeader*>(channel->region_);
    h->magic = AC_FRAME_MAGIC;
    h->version = AC_FRAME_VERSION;
    h->width = width;
    h->height = height;
    h->format = AC_FRAME_FORMAT_RGBA8888;
    h->slot_count = slot_count;
    h->slot_stride = ac_frame_slot_size(width, height, AC_FRAME_FORMAT_RGBA8888);
    channel->header_ = h;

    AC_LOGI("frame channel ready: %ux%u x%u slots (%zu KiB)",
            width, height, slot_count, total / 1024);
    return channel;
}

namespace {

std::unique_ptr<FrameChannelHost> g_host_channel;

} // namespace

FrameChannelHost* HostChannel() {
    return g_host_channel.get();
}

void SetHostChannel(std::unique_ptr<FrameChannelHost> channel) {
    g_host_channel = std::move(channel);
}

bool FrameChannelHost::ReadLatest(uint8_t* dst, size_t dst_len, Snapshot* out) {
    const AcFrameRegionHeader* h = header_;
    if (h == nullptr || h->magic != AC_FRAME_MAGIC) return false;

    const size_t need = (size_t)h->width * h->height * 4;
    if (dst_len < need) return false;

    /* Scan all slots for the highest published sequence. Odd values mean
     * "producer writing" and are skipped. Single producer makes this
     * safe; we only ever copy from one slot. */
    uint64_t best_seq = 0;
    uint32_t best_slot = UINT32_MAX;
    for (uint32_t i = 0; i < h->slot_count; ++i) {
        uint64_t seq = __atomic_load_n(&SlotMeta(region_, h, i)->sequence,
                                       __ATOMIC_ACQUIRE);
        if ((seq & 1) == 0 && seq > best_seq) {
            best_seq = seq;
            best_slot = i;
        }
    }
    if (best_slot == UINT32_MAX) return false;

    AcFrameSlotMeta* meta = SlotMeta(region_, h, best_slot);
    const uint8_t* pixels = SlotPixels(region_, h, best_slot);

    /* Seqlock validation. */
    memcpy(dst, pixels, need);
    uint64_t after = __atomic_load_n(&meta->sequence, __ATOMIC_ACQUIRE);
    if (after != best_seq) return false;

    if (out != nullptr) {
        out->sequence = best_seq;
        out->width = h->width;
        out->height = h->height;
    }
    return true;
}

} // namespace accore
