/*
 * Guest -> host pathname translation.
 *
 * Every absolute guest path "/foo/bar" is mapped to "<sandbox>/foo/bar".
 * Relative paths and anything already inside the sandbox pass through
 * unchanged, which makes the mapping idempotent: paths produced by
 * realpath()/readlink() that already carry the sandbox prefix resolve
 * correctly on the next open() without double-mapping.
 */
#ifndef PATH_REDIRECT_H
#define PATH_REDIRECT_H

#include <stddef.h>

namespace acfake {

enum class MapResult {
    Passthrough, /* use original path as-is */
    Mapped,      /* out[] holds the sandbox-backed path */
    Overflow,    /* combined length exceeds buffer; caller decides */
};

/* Translate one path. Pure function of engine state + inputs (no malloc). */
MapResult map_path(const char* path, char* out, size_t out_size);

/*
 * True when `path` belongs to a virtual filesystem emulated in userspace
 * (/proc, /sys). Such paths must NOT be redirected to the sandbox; the
 * vfs emulation layer owns them.
 */
bool is_virtual_fs_path(const char* path);

/* Register extra passthrough prefixes ("a:/b:c", max 16 x 256 bytes).
 * Called by the state initializer after env parsing. */
void set_excluded_prefixes(const char* colon_list);

} // namespace acfake

#endif /* PATH_REDIRECT_H */
