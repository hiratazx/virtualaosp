/*
 * C-linkage facade over PathResolver so STL-free translation units
 * (notably path_redirect.cpp under the host unit-test harness) can use
 * the alias table without dragging in <string>/<vector>.
 */
#ifndef PATH_RESOLVER_C_H
#define PATH_RESOLVER_C_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True once PathResolver::init() has run in this process. */
bool ac_path_resolver_ready(void);

/*
 * Attempts an alias-table lookup (/etc, /bin, /xbin, ... -> sandbox).
 * Returns true and fills out[] when redirected; false otherwise.
 */
bool ac_path_resolver_resolve(const char* path, char* out, size_t out_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PATH_RESOLVER_C_H */
