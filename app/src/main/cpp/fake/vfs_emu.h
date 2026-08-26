/*
 * Userspace emulation of /proc and /sys.
 *
 * Design:
 *  - A whitelist of virtual entries is served from generated content;
 *    every other /proc|/sys path returns ENOENT so host identity data
 *    cannot leak into the guest.
 *  - Dynamic per-process files (/proc/self/status, maps) stream the REAL
 *    file through a sanitizer into a memfd: identity lines are rewritten
 *    to root and sandbox paths are un-mapped back to guest paths.
 *  - Directory listings come from a static placeholder tree materialized
 *    under <sandbox>/.virtual/{proc,sys}; regular-file placeholders are
 *    shadowed at open() time by memfd content.
 */
#ifndef VFS_EMU_H
#define VFS_EMU_H

#include <stddef.h>
#include <sys/stat.h>

struct DIR;

namespace acfake {

/* Attempt to serve `path` as a virtual file. Returns a readable fd on
 * success, -1 with errno=ENOENT when the path is unknown inside a virtual
 * tree, and -2 when the path is not part of the emulated surface. */
int emu_open(const char* path);

/* Tri-state like emu_open(): 0 handled (st filled), -1 ENOENT, -2 n/a. */
int emu_stat(const char* path, struct stat* st);
bool emu_access(const char* path, int mode, int* err_out);
bool emu_readlink(const char* path, char* buf, size_t size, ssize_t* len_out);

/* Directory listing: maps /proc|/sys paths onto the placeholder tree.
 * Sets handled=true only for paths inside the virtual trees. */
DIR* emu_opendir(const char* path, bool* handled);

} // namespace acfake

#endif /* VFS_EMU_H */
