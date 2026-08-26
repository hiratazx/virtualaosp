add_library(fake SHARED
    fake.cpp
    fake_state.cpp
    path_redirect.cpp
    real_libc.cpp
    syscall_hooks_read.cpp
    syscall_hooks_write.cpp
    uid_spoof.cpp
    vfs_emu.cpp
    mount_emu.cpp
    seccomp_filter.cpp
)

target_link_libraries(fake PRIVATE
    container_common
    log
)

# libfake is LD_PRELOAD'ed into guest processes. It must be self-contained:
# no STL, no exceptions, no RTTI. Hooked symbols are exported with default
# visibility via AC_EXPORT; everything else stays hidden.
target_compile_options(fake PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -fno-exceptions -fno-rtti
    -fvisibility=hidden
    -fomit-frame-pointer
    -ffunction-sections -fdata-sections
)

target_link_options(fake PRIVATE
    -Wl,--gc-sections
    -Wl,-z,max-page-size=16384
)
