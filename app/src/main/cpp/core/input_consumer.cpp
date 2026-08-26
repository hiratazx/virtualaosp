#include "input_consumer.h"
#include "ipc.h"

#include "ac_ipc_protocol.h"

#include <algorithm>
#include <cstring>

namespace accore {

void InputConsumer::setHostSurfaceSize(int width, int height) {
    mHostWidth.store(width > 0 ? width : 1, std::memory_order_relaxed);
    mHostHeight.store(height > 0 ? height : 1, std::memory_order_relaxed);
}

InputConsumer& InputConsumer::getInstance() {
    static InputConsumer instance;
    return instance;
}

int InputConsumer::dispatchTouch(uint32_t action, uint32_t pointerId,
                                 uint32_t xFixed, uint32_t yFixed,
                                 uint32_t pressure) {
    struct AcTouchEvent ev {};
    ev.action = action;
    ev.pointer_id = pointerId;
    ev.x_fixed = xFixed;
    ev.y_fixed = yFixed;
    ev.pressure = pressure;

    auto& server = GlobalIpcServer();
    int delivered = static_cast<int>(server.Broadcast(
        AC_IPC_INPUT_TOUCH,
        reinterpret_cast<const uint8_t*>(&ev),
        static_cast<uint32_t>(sizeof(ev))));
    return delivered;
}

size_t InputConsumer::connectedGuests() const {
    return GlobalIpcServer().client_count();
}

namespace {

uint32_t normalizePixel(float px, int span) {
    float clamped = std::clamp(px, 0.0f, static_cast<float>(span));
    return static_cast<uint32_t>((clamped / span) * 65535.0f);
}

} // namespace

int InputConsumer::dispatchTouchEventBatch(uint32_t action, uint32_t pointerCount,
                                           const uint32_t* pointerIds,
                                           const float* xCoords,
                                           const float* yCoords,
                                           const float* pressures) {
    if (pointerCount == 0 || pointerIds == nullptr || xCoords == nullptr ||
        yCoords == nullptr || pressures == nullptr) {
        return 0;
    }

    auto& server = GlobalIpcServer();
    int delivered = 0;
    const int w = mHostWidth.load(std::memory_order_relaxed);
    const int h = mHostHeight.load(std::memory_order_relaxed);

    for (uint32_t i = 0; i < pointerCount; ++i) {
        struct AcTouchEvent ev {};
        ev.action = action;
        ev.pointer_id = pointerIds[i];
        ev.x_fixed = normalizePixel(xCoords[i], w);
        ev.y_fixed = normalizePixel(yCoords[i], h);
        ev.pressure = static_cast<uint32_t>(
            std::clamp(pressures[i], 0.0f, 1.0f) * 65535.0f);
        delivered += static_cast<int>(server.Broadcast(
            AC_IPC_INPUT_TOUCH,
            reinterpret_cast<const uint8_t*>(&ev),
            static_cast<uint32_t>(sizeof(ev))));
    }
    return delivered;
}

int InputConsumer::dispatchKeyEvent(uint32_t keycode, bool isDown) {
    struct AcKeyEvent ev {};
    ev.keycode = keycode;
    ev.action = isDown ? 0u : 1u;
    return static_cast<int>(GlobalIpcServer().Broadcast(
        AC_IPC_INPUT_KEY,
        reinterpret_cast<const uint8_t*>(&ev),
        static_cast<uint32_t>(sizeof(ev))));
}

} // namespace accore
