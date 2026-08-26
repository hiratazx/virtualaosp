/*
 * libcontainer_core.so — JNI bridge of the host-side container coordinator.
 *
 * Owns JNI glue; subsystems (launcher, IPC, graphics bridge) plug in from
 * their own translation units.
 */
#include "launcher.h"

#include <jni.h>
#include <cstring>
#include <string>

#define CORE_VERSION "0.1.0"

namespace {

using accore::ContainerState;
using accore::GuestLauncher;
using accore::LaunchConfig;

std::string ToStd(JNIEnv* env, jstring s) {
    if (s == nullptr) return {};
    const char* chars = env->GetStringUTFChars(s, nullptr);
    if (chars == nullptr) return {};
    std::string result(chars);
    env->ReleaseStringUTFChars(s, chars);
    return result;
}

} // namespace

extern "C" {

JNIEXPORT jstring JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerCore_nativeVersion(
        JNIEnv* env, jobject /*thiz*/) {
    return env->NewStringUTF(CORE_VERSION);
}

JNIEXPORT jboolean JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerCore_nativePing(
        JNIEnv* /*env*/, jobject /*thiz*/) {
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerCore_nativeStartContainer(
        JNIEnv* env, jobject /*thiz*/,
        jstring rootfs_dir, jstring native_lib_dir, jstring init_path,
        jstring extra_mounts, jstring exclude_paths,
        jint fake_uid, jint fake_gid, jboolean enable_seccomp) {

    LaunchConfig cfg;
    cfg.rootfs_dir = ToStd(env, rootfs_dir);
    cfg.native_lib_dir = ToStd(env, native_lib_dir);
    cfg.init_path = ToStd(env, init_path);
    cfg.extra_mounts = ToStd(env, extra_mounts);
    cfg.exclude_paths = ToStd(env, exclude_paths);
    cfg.fake_uid = fake_uid;
    cfg.fake_gid = fake_gid;
    cfg.enable_seccomp = enable_seccomp == JNI_TRUE;

    GuestLauncher::SetState(ContainerState::kStarting);
    pid_t pid = GuestLauncher::Start(cfg);
    if (pid <= 0) {
        GuestLauncher::SetState(ContainerState::kIdle);
    }
    return static_cast<jint>(pid);
}

JNIEXPORT jboolean JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerCore_nativeStopContainer(
        JNIEnv* /*env*/, jobject /*thiz*/, jint pid, jint grace_ms) {
    return GuestLauncher::Stop(pid, grace_ms) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerCore_nativeGetState(
        JNIEnv* /*env*/, jobject /*thiz*/) {
    return static_cast<jint>(GuestLauncher::state());
}
}
