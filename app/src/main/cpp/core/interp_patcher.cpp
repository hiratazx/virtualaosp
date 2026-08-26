#include "interp_patcher.h"
#include "log.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace accore {

namespace {

#pragma pack(push, 1)
struct Elf64Ehdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64Phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};
#pragma pack(pop)

constexpr uint16_t kElfClass64 = 2;
constexpr size_t kIdentClassIdx = 4;
constexpr uint32_t kPtInterp = 3;

} // namespace

int ReadInterp(const std::string& elf_path, std::string* out) {
    int fd = open(elf_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -errno;

    Elf64Ehdr eh;
    if (pread(fd, &eh, sizeof(eh), 0) != sizeof(eh)) {
        close(fd);
        return errno ? -errno : -EIO;
    }
    if (memcmp(eh.e_ident, "\x7f" "ELF", 4) != 0 ||
        eh.e_ident[kIdentClassIdx] != kElfClass64) {
        close(fd);
        return -EINVAL;
    }

    for (uint16_t i = 0; i < eh.e_phnum; ++i) {
        Elf64Phdr ph;
        off_t off = static_cast<off_t>(eh.e_phoff) +
                    static_cast<off_t>(i) * eh.e_phentsize;
        if (pread(fd, &ph, sizeof(ph), off) != sizeof(ph)) {
            close(fd);
            return -EIO;
        }
        if (ph.p_type == kPtInterp) {
            std::string interp(static_cast<size_t>(ph.p_filesz), '\0');
            if (pread(fd, interp.data(), ph.p_filesz,
                      static_cast<off_t>(ph.p_offset)) !=
                static_cast<ssize_t>(ph.p_filesz)) {
                close(fd);
                return -EIO;
            }
            interp.resize(strlen(interp.c_str()));
            *out = std::move(interp);
            close(fd);
            return 0;
        }
    }
    close(fd);
    return -EINVAL;
}

int PatchInterp(const std::string& elf_path, const std::string& new_interp) {
    int fd = open(elf_path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) return -errno;

    Elf64Ehdr eh;
    if (pread(fd, &eh, sizeof(eh), 0) != sizeof(eh)) {
        close(fd);
        return errno ? -errno : -EIO;
    }
    if (memcmp(eh.e_ident, "\x7f" "ELF", 4) != 0 ||
        eh.e_ident[kIdentClassIdx] != kElfClass64) {
        close(fd);
        return -EINVAL;
    }

    for (uint16_t i = 0; i < eh.e_phnum; ++i) {
        Elf64Phdr ph;
        off_t off = static_cast<off_t>(eh.e_phoff) +
                    static_cast<off_t>(i) * eh.e_phentsize;
        if (pread(fd, &ph, sizeof(ph), off) != sizeof(ph)) {
            close(fd);
            return -EIO;
        }
        if (ph.p_type != kPtInterp) continue;

        /* Room for path + NUL terminator inside the current segment. */
        if (new_interp.size() + 1 > ph.p_filesz) {
            close(fd);
            AC_LOGE("interp does not fit (%zu > %llu) in %s",
                    new_interp.size(),
                    static_cast<unsigned long long>(ph.p_filesz),
                    elf_path.c_str());
            return -ENOSPC;
        }

        std::string padded(static_cast<size_t>(ph.p_filesz), '\0');
        memcpy(padded.data(), new_interp.data(), new_interp.size());
        if (pwrite(fd, padded.data(), padded.size(),
                   static_cast<off_t>(ph.p_offset)) !=
            static_cast<ssize_t>(padded.size())) {
            close(fd);
            return -EIO;
        }
        close(fd);
        AC_LOGI("patched interp of %s -> %s", elf_path.c_str(), new_interp.c_str());
        return 0;
    }
    close(fd);
    return -EINVAL;
}

} // namespace accore
