/*
 * Linux touch input consumer.
 *
 * Normalized host-side touch packets enter here and are fanned out to
 * every connected guest over the primary IPC server; the guest-side
 * client (libfake ipc_client) spools them for the input framework
 * helper. Coordinates arrive as 16.16 fixed point per AcTouchEvent.
 */
#ifndef INPUT_CONSUMER_H
#define INPUT_CONSUMER_H

#include <cstdint>

namespace accore {

class InputConsumer {
public:
    static InputConsumer& getInstance();

    /* Returns the number of guests the event was delivered to. */
    int dispatchTouch(uint32_t action, uint32_t pointerId,
                      uint32_t xFixed, uint32_t yFixed,
                      uint32_t pressure);

    size_t connectedGuests() const;
};

} // namespace accore

#endif /* INPUT_CONSUMER_H */
