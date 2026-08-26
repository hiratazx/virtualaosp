#include "input_consumer.h"
#include "ipc.h"

#include "ac_ipc_protocol.h"

#include <cstring>

namespace accore {

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

} // namespace accore
