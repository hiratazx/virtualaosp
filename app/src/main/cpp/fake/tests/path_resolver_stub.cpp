/*
 * Test double for the PathResolver C facade: keeps the host unit-test
 * harness free of libc++ requirements while letting path_redirect.cpp
 * link. The alias table is intentionally inert under test — the suite
 * validates the generic redirection engine, and resolver delegation is
 * skipped whenever ac_path_resolver_ready() reports false.
 */
#include "path_resolver_c.h"

bool ac_path_resolver_ready(void) {
    return false;
}

bool ac_path_resolver_resolve(const char* path, char* out, size_t out_size) {
    (void)path;
    (void)out;
    (void)out_size;
    return false;
}
