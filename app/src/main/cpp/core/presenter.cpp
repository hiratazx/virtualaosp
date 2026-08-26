#include "presenter.h"
#include "frame_channel.h"
#include "log.h"

#include <android/native_window.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace accore {

namespace {

constexpr auto kTargetInterval = std::chrono::milliseconds(16); /* ~60 Hz */

std::thread g_loop;
std::atomic<bool> g_running{false};
ANativeWindow* g_window = nullptr;

void BlitScaled(const uint8_t* src, uint32_t sw, uint32_t sh,
                uint8_t* dst, uint32_t dw, uint32_t dh, size_t dst_stride) {
    /* Nearest-neighbor: fine for a software path; GPU composition can
     * replace it once the EGL wrapper lands (Phase 3.3). */
    const uint64_t xr = (static_cast<uint64_t>(sw) << 32) / dw;
    const uint64_t yr = (static_cast<uint64_t>(sh) << 32) / dh;
    for (uint32_t y = 0; y < dh; ++y) {
        const uint8_t* srow = src + ((y * yr) >> 32) * sw * 4;
        uint8_t* drow = dst + static_cast<size_t>(y) * dst_stride;
        uint64_t sx = 0;
        for (uint32_t x = 0; x < dw; ++x) {
            memcpy(drow + x * 4, srow + ((sx >> 32) & 0xFFFFFFFFu) * 4, 4);
            sx += xr;
        }
    }
}

} // namespace

int FramePresenter::Attach(ANativeWindow* window) {
    if (window == nullptr) return -EINVAL;
    if (g_running.exchange(true)) {
        return -EALREADY; /* already presenting */
    }

    /* Own a reference for the lifetime of the loop. */
    g_window = window;
    ANativeWindow_acquire(g_window);

    int32_t rc = ANativeWindow_setBuffersGeometry(
        window, 0, 0, WINDOW_FORMAT_RGBA_8888);
    if (rc != 0) {
        g_running = false;
        AC_LOGE("setBuffersGeometry failed rc=%d", rc);
        return -EIO;
    }

    g_loop = std::thread([window] { Loop(window); });
    return 0;
}

void FramePresenter::Loop(ANativeWindow* window) {
    FrameChannelHost* channel = HostChannel();
    if (channel == nullptr) {
        AC_LOGW("presenter running without a frame channel");
    }

    uint32_t last_seq = 0;

    while (g_running.load(std::memory_order_relaxed)) {
        const auto next_tick = std::chrono::steady_clock::now() + kTargetInterval;

        if (channel != nullptr && ANativeWindow_getWidth(window) > 0) {
            const uint32_t dw = static_cast<uint32_t>(ANativeWindow_getWidth(window));
            const uint32_t dh = static_cast<uint32_t>(ANativeWindow_getHeight(window));
            const size_t need = static_cast<size_t>(channel->header()->width) *
                                channel->header()->height * 4;

            thread_local std::vector<uint8_t> frame;
            frame.resize(need);

            FrameChannelHost::Snapshot snap;
            if (channel->ReadLatest(frame.data(), frame.size(), &snap) &&
                snap.sequence != last_seq) {
                last_seq = snap.sequence;

                ANativeWindow_Buffer buf;
                if (ANativeWindow_lock(window, &buf, nullptr) == 0) {
                    BlitScaled(frame.data(), snap.width, snap.height,
                               static_cast<uint8_t*>(buf.bits),
                               static_cast<uint32_t>(buf.width),
                               static_cast<uint32_t>(buf.height),
                               static_cast<size_t>(buf.stride) * 4);
                    ANativeWindow_unlockAndPost(window);
                }
            }
        }

        std::this_thread::sleep_until(next_tick);
    }
}

void FramePresenter::Detach() {
    if (!g_running.exchange(false)) {
        return;
    }
    if (g_loop.joinable()) {
        g_loop.join();
    }
    if (g_window != nullptr) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }
}

} // namespace accore
