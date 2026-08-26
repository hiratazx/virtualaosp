/*
 * libcontainer_core.so — JNI bridge of the host-side container coordinator.
 *
 * Owns JNI glue; subsystems (launcher, IPC, graphics bridge) plug in from
 * their own translation units.
 */
#include "launcher.h"
#include "ipc.h"
#include "log.h"
#include "frame_channel.h"
#include "presenter.h"

#include <android/native_window_jni.h>
#include <cerrno>
#include <jni.h>
#include <cstdint>
#include <cstring>
#include <memory>
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

/* Bridges guest messages into logcat until dedicated subsystems
 * (input/frame pipelines) register their own handlers in later phases. */
class LoggingIpcHandler : public accore::IpcMessageHandler {
public:
    void OnMessage(int client_fd, uint16_t type,
                   const uint8_t* data, uint32_t len) override {
        AC_LOGD("ipc msg fd=%d type=0x%02x len=%u", client_fd, type, len);
        (void)data;
    }
};

accore::IpcServer& Ipc() {
    static accore::IpcServer server;
    return server;
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
        jint fake_uid, jint fake_gid, jint frame_fd, jboolean enable_seccomp) {

    LaunchConfig cfg;
    cfg.rootfs_dir = ToStd(env, rootfs_dir);
    cfg.native_lib_dir = ToStd(env, native_lib_dir);
    cfg.init_path = ToStd(env, init_path);
    cfg.extra_mounts = ToStd(env, extra_mounts);
    cfg.exclude_paths = ToStd(env, exclude_paths);
    cfg.fake_uid = fake_uid;
    cfg.fake_gid = fake_gid;
    cfg.frame_fd = frame_fd;
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

JNIEXPORT jboolean JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerCore_nativeIpcStart(
        JNIEnv* env, jobject /*thiz*/, jstring socket_path) {
    std::string path = ToStd(env, socket_path);
    if (path.empty()) return JNI_FALSE;
    int rc = Ipc().Start(path, std::make_shared<LoggingIpcHandler>());
    return rc == 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerCore_nativeIpcStop(
        JNIEnv* /*env*/, jobject /*thiz*/) {
    Ipc().Stop();
}

JNIEXPORT jint JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerCore_nativeIpcBroadcast(
        JNIEnv* env, jobject /*thiz*/, jint type, jbyteArray payload) {
    if (payload == nullptr) {
        return static_cast<jint>(Ipc().Broadcast(static_cast<uint16_t>(type), nullptr, 0));
    }
    jsize len = env->GetArrayLength(payload);
    jbyte* bytes = env->GetByteArrayElements(payload, nullptr);
    jint delivered = static_cast<jint>(Ipc().Broadcast(
        static_cast<uint16_t>(type),
        reinterpret_cast<const uint8_t*>(bytes), static_cast<uint32_t>(len)));
    env->ReleaseByteArrayElements(payload, bytes, JNI_ABORT);
    return delivered;
}

/* ------------------------------------------------------------------ */
/* frame channel + presentation                                        */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerCore_nativeCreateFrameChannel(
        JNIEnv* /*env*/, jobject /*thiz*/, jint width, jint height, jint slots) {
    auto ch = accore::FrameChannelHost::Create(static_cast<uint32_t>(width),
                                               static_cast<uint32_t>(height),
                                               static_cast<uint32_t>(slots));
    if (ch == nullptr) return -EINVAL;
    accore::SetHostChannel(std::move(ch));
    return accore::HostChannel()->fd(); /* owned by the singleton; fd stays open */
}

JNIEXPORT void JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerCore_nativeCloseFrameChannel(
        JNIEnv* /*env*/, jobject /*thiz*/) {
    accore::FramePresenter::Detach();
    accore::SetHostChannel(nullptr);
}

JNIEXPORT jboolean JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerCore_nativePresenterAttachSurface(
        JNIEnv* env, jobject /*thiz*/, jobject surface) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) return JNI_FALSE;
    int rc = accore::FramePresenter::Attach(window);
    ANativeWindow_release(window); /* presenter holds its own ref */
    return rc == 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerCore_nativePresenterDetach(
        JNIEnv* /*env*/, jobject /*thiz*/) {
    accore::FramePresenter::Detach();
}
}
