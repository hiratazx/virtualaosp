/*
 * Centralized dlsym(RTLD_NEXT) accessors for the handful of libc symbols
 * that libfake's internal machinery needs to call in their REAL form
 * (bypassing our own exported hooks). One lookup table avoids scattering
 * raw dlsym calls across subsystems.
 */
#ifndef REAL_LIBC_H
#define REAL_LIBC_H

#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace acfake {

int real_open(const char* path, int flags, mode_t mode);
int real_close(int fd);
ssize_t real_read(int fd, void* buf, size_t count);
off_t real_lseek(int fd, off_t offset, int whence);
ssize_t real_readlink(const char* path, char* buf, size_t size);
int real_fstat(int fd, struct stat* st);
FILE* real_fdopen(int fd, const char* mode);
DIR* real_opendir(const char* path);
pid_t real_getpid();

} // namespace acfake

#endif /* REAL_LIBC_H */
