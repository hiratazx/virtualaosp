add_library(container_core SHARED
    container_core.cpp
    launcher.cpp
    ipc.cpp
    frame_channel.cpp
)

target_link_libraries(container_core PRIVATE
    container_common
    log
    android
)

target_compile_options(container_core PRIVATE
    -Wall -Wextra -Wno-unused-parameter
)
