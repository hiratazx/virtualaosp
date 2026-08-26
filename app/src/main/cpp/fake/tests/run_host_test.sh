#!/usr/bin/env bash
# Host-side unit tests for libfake's pure logic (no Android device needed).
# Uses the NDK clang driver against host glibc. Requires $ANDROID_NDK_HOME
# or ndk-r28b at ~/ndk-r28b.
set -euo pipefail

NDK="${ANDROID_NDK_HOME:-$HOME/ndk-r28b}"
CLANG="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++"
LIBCDEV="${AC_LIBCDEV:-$HOME/.local/opt/libcdev}"

INC="$LIBCDEV/usr/include"
LIB="$LIBCDEV/usr/lib/x86_64-linux-gnu"
GCCDIR="$(dirname "$(find "$LIBCDEV/usr/lib" -name 'crtbeginS.o' | head -1)")"

SRC_DIR="$(cd "$(dirname "$0")/.." && pwd)"

"$CLANG" \
    --target=x86_64-unknown-linux-gnu -fuse-ld=lld \
    -B "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin" \
    -nostdinc++ -nodefaultlibs -nostartfiles -std=c++20 -Wall -Wextra \
    -isystem "$INC" -isystem "$INC/x86_64-linux-gnu" \
    -I "$SRC_DIR" -I "$SRC_DIR/../common/include" \
    "$LIB/Scrt1.o" "$LIB/crti.o" "$GCCDIR/crtbeginS.o" \
    "$@" \
    -o /tmp/ac_host_test_bin \
    -L"$LIB" -L"$GCCDIR" -L/usr/lib/x86_64-linux-gnu \
    $(test -f "$LIBCDEV/local_libc.so" && echo "$LIBCDEV/local_libc.so" || echo -lc) \
    -lgcc -lgcc_eh -lm -lpthread \
    "$GCCDIR/crtendS.o" "$LIB/crtn.o"

exec /tmp/ac_host_test_bin
