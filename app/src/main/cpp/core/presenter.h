/*
 * Frame presentation: pumps stable frames from the shared channel onto an
 * Android Surface (ANativeWindow) at a fixed cadence approximating the
 * display refresh. Software blit with nearest-neighbor scaling so guest
 * resolution can differ from the host view.
 */
#ifndef PRESENTER_H
#define PRESENTER_H

#include <cstdint>

struct ANativeWindow;

namespace accore {

class FramePresenter {
public:
    ~FramePresenter();

    /* Starts the presentation thread. Takes a strong reference to the
     * window. Returns -errno style codes on failure. */
    static int Attach(ANativeWindow* window);

    /* Stops the thread and releases the window reference. */
    static void Detach();

private:
    static void Loop(ANativeWindow* window);
};

} // namespace accore

#endif /* PRESENTER_H */
