/* Shared helpers for identity spoofing. */
#ifndef UID_SPOOF_H
#define UID_SPOOF_H

#include <sys/stat.h>

namespace acfake {

/* Rewrites st_uid/st_gid to the spoofed root identity. Applied by the
 * stat hooks to sandbox-backed paths so guest code sees coherent
 * ownership (all container files are kernel-owned by the host app). */
void fix_stat_owner(struct stat* st);

} // namespace acfake

#endif /* UID_SPOOF_H */
