#include "display_renderer.h"
#include "frame_channel.h"
#include "presenter.h"
#include "log.h"

#include <cerrno>

namespace accore {

DisplayRenderer& DisplayRenderer::getInstance() {
    static DisplayRenderer instance;
    return instance;
}

int DisplayRenderer::createChannel(uint32_t width, uint32_t height, uint32_t slots) {
    auto channel = FrameChannelHost::Create(width, height, slots);
    if (channel == nullptr) return -EINVAL;
    SetHostChannel(std::move(channel));
    return 0;
}

bool DisplayRenderer::attach(ANativeWindow* window) {
    if (window == nullptr) return false;

    if (HostChannel() == nullptr) {
        /* Default guest geometry until an explicit configuration lands. */
        if (createChannel(720, 1280, 4) != 0) {
            AC_LOGE("failed to create default frame channel");
            return false;
        }
    }

    if (FramePresenter::Attach(window) != 0) {
        return false;
    }
    mAttached = true;
    return true;
}

void DisplayRenderer::detach() {
    FramePresenter::Detach();
    mAttached = false;
}

} // namespace accore
