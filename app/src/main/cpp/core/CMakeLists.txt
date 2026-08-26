add_library(container_core SHARED
    container_core.cpp
    launcher.cpp
    ipc.cpp
    frame_channel.cpp
    interp_patcher.cpp
    guest_launcher.cpp
    display_renderer.cpp
    input_consumer.cpp
    native_bridge.cpp
)

target_link_libraries(container_core PRIVATE
    container_common
    log
    android
    EGL
    GLESv3
)

target_compile_options(container_core PRIVATE
    -Wall -Wextra -Wno-unused-parameter
)
