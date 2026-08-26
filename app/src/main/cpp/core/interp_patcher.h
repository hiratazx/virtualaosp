/*
 * ELF64 PT_INTERP inspection/in-place patching for container ROMs.
 *
 * Guest executables carry an interpreter path resolved DIRECTLY BY THE
 * KERNEL at execve() time — outside the reach of our interception layer.
 * Container ROM builds therefore either pre-bake the sandbox-absolute
 * linker path (patchelf --set-interpreter at image build time) or get
 * migrated here during import.
 *
 * In-place patching only succeeds when the replacement fits inside the
 * existing .interp segment (NUL padded); otherwise callers must rebuild
 * the image — surfaced as -ENOTSUP.
 */
#ifndef INTERP_PATCHER_H
#define INTERP_PATCHER_H

#include <string>

namespace accore {

/* Reads the current interpreter into `out`. Returns 0 or -errno. */
int ReadInterp(const std::string& elf_path, std::string* out);

/*
 * Overwrites PT_INTERP with `new_interp` (NUL padded). Returns:
 *   0        success
 *   -EINVAL  not an ELF64 / no PT_INTERP segment
 *   -ENOSPC  new path does not fit the existing segment
 *   -errno   I/O failures
 */
int PatchInterp(const std::string& elf_path, const std::string& new_interp);

} // namespace accore

#endif /* INTERP_PATCHER_H */
