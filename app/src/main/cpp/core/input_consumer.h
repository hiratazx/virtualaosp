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
#include <atomic>

namespace accore {

class InputConsumer {
public:
    static InputConsumer& getInstance();

    /* Host surface geometry for pixel -> 16.16 normalization; updated on
     * every surfaceChanged so orientation/multi-window changes rescale
     * touch coordinates automatically. */
    void setHostSurfaceSize(int width, int height);

    /* Legacy single-pointer entry (16.16 coords pre-normalized). */
    int dispatchTouch(uint32_t action, uint32_t pointerId,
                      uint32_t xFixed, uint32_t yFixed,
                      uint32_t pressure);

    /* Multi-pointer batch in HOST PIXELS; normalized against the live
     * surface size at dispatch time (dynamic orientation scaling). */
    int dispatchTouchEventBatch(uint32_t action, uint32_t pointerCount,
                                const uint32_t* pointerIds,
                                const float* xCoords,
                                const float* yCoords,
                                const float* pressures);

    /* Android keycode; isDown mirrors KeyEvent down/up semantics. */
    int dispatchKeyEvent(uint32_t keycode, bool isDown);

    size_t connectedGuests() const;

private:
    std::atomic<int> mHostWidth{1080};
    std::atomic<int> mHostHeight{2400};
};

} // namespace accore

#endif /* INPUT_CONSUMER_H */
