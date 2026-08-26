/*
 * SECCOMP-BPF safety net for syscalls that cannot be reliably interposed
 * (direct syscall() invocations bypassing the PLT). The filter denies the
 * privileged operation set with deterministic EPERM instead of letting
 * them fail unpredictably — or worse, partially succeed.
 *
 * Opt-in via AC_SECCOMP=1 so bring-up/debugging can run unfiltered.
 */
#ifndef SECCOMP_FILTER_H
#define SECCOMP_FILTER_H

namespace acfake {

/* Install the denylist filter for the current process. Idempotent and
 * non-fatal on failure (logged). Returns true when installed. */
bool install_seccomp_filter();

} // namespace acfake

#endif /* SECCOMP_FILTER_H */
