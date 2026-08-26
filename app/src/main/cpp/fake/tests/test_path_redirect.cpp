/* Host-side unit tests for acfake::map_path — no Android dependencies. */
#include "path_redirect.h"
#include "fake_state.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        ++g_failures; \
        printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define ROOTFS "/data/user/0/dev.itzkaguya.aospcontainer/files/rootfs"

static void expect_map(const char* in, const char* want) {
    char out[512];
    auto r = acfake::map_path(in, out, sizeof(out));
    if (want == NULL) {
        CHECK(r == acfake::MapResult::Passthrough);
    } else {
        CHECK(r == acfake::MapResult::Mapped);
        if (r == acfake::MapResult::Mapped) {
            CHECK(strcmp(out, want) == 0);
        }
    }
}

int main() {
    /* 1. Engine disabled => everything passes through untouched. */
    acfake::set_state(false, "/", 0, 0);
    expect_map("/system/x", NULL);

    /* 2. Enabled with trailing-slash normalization. */
    acfake::set_state(true, ROOTFS "///", 0, 0);

    expect_map("/system/framework/foo.jar", ROOTFS "/system/framework/foo.jar");
    expect_map("/", ROOTFS "/");
    expect_map("/data/local/tmp", ROOTFS "/data/local/tmp");

    /* 3. Idempotency: sandbox-internal paths are never re-mapped. */
    expect_map(ROOTFS "/system/framework/foo.jar", NULL);
    expect_map(ROOTFS, NULL);

    /* 4. Virtual filesystems bypass raw redirection. */
    expect_map("/proc/self/status", NULL);
    expect_map("/proc", NULL);
    expect_map("/processor_info", ROOTFS "/processor_info"); /* prefix boundary */
    expect_map("/sys/class/thermal/cooling0", NULL);
    expect_map("/sysrq-trigger", ROOTFS "/sysrq-trigger");

    /* 5. Relative paths untouched. */
    expect_map("relative/path.txt", NULL);

    /* 6. Overflow detection. */
    char big[8192];
    big[0] = '/';
    memset(big + 1, 'a', sizeof(big) - 2);
    big[sizeof(big) - 1] = '\0';
    {
        char tiny[16];
        CHECK(acfake::map_path(big, tiny, sizeof(tiny)) == acfake::MapResult::Overflow);
    }

    /* 7. Extra exclusion prefixes with boundary checks. */
    acfake::set_excluded_prefixes("/mnt/runtime:/hostfs");
    expect_map("/mnt/runtime/inner/file", NULL);
    expect_map("/mnt/runtimeX/file", ROOTFS "/mnt/runtimeX/file");
    expect_map("/hostfs", NULL);
    expect_map("/system/other", ROOTFS "/system/other");

    /* 8. Disable clears state. */
    acfake::set_state(false, NULL, 5, 5);
    expect_map("/system/x", NULL);

    /* 9. Reverse mapping (unmap_path). */
    acfake::set_state(true, ROOTFS, 0, 0);
    {
        char out[512];
        CHECK(acfake::unmap_path(ROOTFS "/system/lib/libc.so", out, sizeof(out)));
        CHECK(strcmp(out, "/system/lib/libc.so") == 0);

        CHECK(acfake::unmap_path(ROOTFS, out, sizeof(out)));
        CHECK(strcmp(out, "/") == 0);

        CHECK(!acfake::unmap_path("/elsewhere/file", out, sizeof(out)));
        CHECK(!acfake::unmap_path(ROOTFS "XX/not-under-root", out, sizeof(out)));

        /* round-trip: map -> unmap == identity */
        char mapped[512], restored[512];
        CHECK(acfake::map_path("/data/app/foo.apk", mapped, sizeof(mapped)) ==
              acfake::MapResult::Mapped);
        CHECK(acfake::unmap_path(mapped, restored, sizeof(restored)));
        CHECK(strcmp(restored, "/data/app/foo.apk") == 0);
    }

    if (g_failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    printf("%d FAILURES\n", g_failures);
    return 1;
}
