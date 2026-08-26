#include "frame_producer.h"
#include "container_common.h"
#include "fake_state.h"
#include "frame_protocol.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

namespace acfake {

namespace {

constexpr size_t kPageSize = 4096;

int g_fd = -1;
uint8_t* g_region = nullptr;
size_t g_region_size = 0;
AcFrameRegionHeader* g_header = nullptr;

uint64_t g_last_sequence = 0;
uint32_t g_next_slot = 0;

AcFrameSlotMeta* slot_meta(uint32_t i) {
    return reinterpret_cast<AcFrameSlotMeta*>(
        reinterpret_cast<uint8_t*>(g_header) + sizeof(AcFrameRegionHeader) +
        (size_t)i * g_header->slot_stride);
}

uint8_t* slot_pixels(uint32_t i) {
    return reinterpret_cast<uint8_t*>(slot_meta(i)) + sizeof(AcFrameSlotMeta);
}

} // namespace

bool frame_producer_init_from_env(void) {
    if (!enabled()) {
        return false;
    }
    const char* fd_str = getenv(AC_ENV_FRAME_FD);
    if (fd_str == nullptr || fd_str[0] == '\0') {
        return false; /* display channel optional during bring-up */
    }

    int fd = static_cast<int>(strtol(fd_str, nullptr, 10));
    if (fcntl(fd, F_GETFD) < 0) {
        AC_LOGE("AC_FRAME_FD %d invalid: %s", fd, strerror(errno));
        return false;
    }

    /* Probe region size from the header itself. */
    AcFrameRegionHeader probe;
    ssize_t n = pread(fd, &probe, sizeof(probe), 0);
    if (n != static_cast<ssize_t>(sizeof(probe)) ||
        probe.magic != AC_FRAME_MAGIC ||
        probe.version != AC_FRAME_VERSION) {
        AC_LOGE("AC_FRAME_FD %d: bad header", fd);
        return false;
    }
    const size_t total =
        ac_frame_region_size(probe.width, probe.height, probe.format,
                             probe.slot_count);

    void* map = mmap(nullptr, ((total + kPageSize - 1) / kPageSize) * kPageSize,
                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        AC_LOGE("frame mmap failed: %s", strerror(errno));
        return false;
    }

    g_fd = fd;
    g_region = static_cast<uint8_t*>(map);
    g_region_size = total;
    g_header = reinterpret_cast<AcFrameRegionHeader*>(map);

    AC_LOGI("frame producer attached: %ux%u x%u slots",
            g_header->width, g_header->height, g_header->slot_count);
    return true;
}

bool frame_producer_active(void) {
    return g_region != nullptr;
}

uint32_t frame_width(void) {
    return g_header ? g_header->width : 0;
}

uint32_t frame_height(void) {
    return g_header ? g_header->height : 0;
}

bool frame_publish(const void* pixels, size_t len) {
    if (g_region == nullptr || pixels == nullptr) {
        return false;
    }
    const size_t expected = (size_t)g_header->width * g_header->height * 4;
    if (len != expected) {
        AC_LOGW("frame_publish size mismatch (%zu != %zu)", len, expected);
        return false;
    }

    AcFrameSlotMeta* meta = slot_meta(g_next_slot);
    uint8_t* dst = slot_pixels(g_next_slot);

    /* Mark slot as being written (odd sequence). */
    __atomic_store_n(&meta->sequence, g_last_sequence + 1, __ATOMIC_RELEASE);
    memcpy(dst, pixels, len);
    __atomic_store_n(&meta->bytes_used, static_cast<uint32_t>(len),
                     __ATOMIC_RELEASE);
    /* Publish: even sequence means stable. */
    g_last_sequence += 2;
    __atomic_store_n(&meta->sequence, g_last_sequence, __ATOMIC_RELEASE);

    g_next_slot = (g_next_slot + 1) % g_header->slot_count;
    return true;
}

} // namespace acfake
