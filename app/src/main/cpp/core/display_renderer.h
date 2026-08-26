/*
 * Display rendering facade.
 *
 * Owns the guest-facing surface lifecycle: guarantees a shared frame
 * channel exists (creating a default-resolution one on demand) and hands
 * ANativeWindow attachments to the vsync-paced presenter that blits
 * seqlock-stable frames from the channel. GPU/EGL composition can replace
 * the software path behind this same facade later.
 */
#ifndef DISPLAY_RENDERER_H
#define DISPLAY_RENDERER_H

#include <cstdint>

struct ANativeWindow;

namespace accore {

class DisplayRenderer {
public:
    static DisplayRenderer& getInstance();

    /* Creates the shared frame region explicitly (optional; attach()
     * falls back to defaults). Returns 0 or -errno. */
    int createChannel(uint32_t width, uint32_t height, uint32_t slots);

    /* Starts presenting onto `window`; acquires its own reference. */
    bool attach(ANativeWindow* window);

    /* Stops the presentation loop and releases the window. */
    void detach();

    bool isAttached() const { return mAttached; }

private:
    DisplayRenderer() = default;
    bool mAttached = false;
};

/*
 * Diagnostic fallback policy: while no guest frame has ever arrived,
 * the presenter clears the surface with an animated #1E1E24 pulse at
 * ~30 FPS instead of leaving a black screen — proving EGL/ANativeWindow
 * operation end to end. Enabled by default.
 */
void setDiagnosticFallbackEnabled(bool enabled);
bool diagnosticFallbackEnabled();

} // namespace accore

#endif /* DISPLAY_RENDERER_H */
