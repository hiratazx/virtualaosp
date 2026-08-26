/*
 * native_bridge.cpp - JNI surface binding Kotlin to the container
 * subsystems: GuestLauncher lifecycle (+ async state callback),
 * DisplayRenderer surface attachment, and InputConsumer touch dispatch.
 *
 * Companion declarations live in ContainerNativeBridge.kt; keep symbol
 * names in sync.
 */
#include "guest_launcher.h"
#include "display_renderer.h"
#include "input_consumer.h"
#include "log.h"

#include <android/native_window_jni.h>
#include <jni.h>
#include <atomic>
#include <cstring>

namespace {

JavaVM* g_vm = nullptr;

std::atomic<jobject> g_listener{nullptr};

jobject AcquireListener() {
    return g_listener.load(std::memory_order_acquire);
}

void InvokeStateCallback(int state, int exit_code) {
    jobject listener = AcquireListener();
    if (listener == nullptr || g_vm == nullptr) return;

    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    }

    jclass clazz = env->GetObjectClass(listener);
    if (jmethodID mid = env->GetMethodID(clazz, "onStateChanged", "(II)V"); mid != nullptr) {
        env->CallVoidMethod(listener, mid,
                            static_cast<jint>(state), static_cast<jint>(exit_code));
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(clazz);

    if (attached) g_vm->DetachCurrentThread();
}

} // namespace

extern "C" JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    g_vm = vm;
    GuestLauncher::getInstance().setStateCallback(
        [](ContainerState state, int exit_code) {
            InvokeStateCallback(static_cast<int>(state), exit_code);
        });
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerNativeBridge_nativeStartContainer(
        JNIEnv* env, jobject /*thiz*/, jstring rootfs_path, jstring libfake_path,
        jstring init_binary_path) {
    auto to_std = [&](jstring s) -> std::string {
        if (s == nullptr) return {};
        const char* c = env->GetStringUTFChars(s, nullptr);
        std::string out = c ? c : "";
        env->ReleaseStringUTFChars(s, c);
        return out;
    };

    LaunchConfig cfg;
    cfg.rootfsPath = to_std(rootfs_path);
    cfg.libfakePath = to_std(libfake_path);
    cfg.initBinaryPath = to_std(init_binary_path);
    if (cfg.rootfsPath.empty() || cfg.initBinaryPath.empty()) return JNI_FALSE;

    return GuestLauncher::getInstance().startContainer(cfg) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerNativeBridge_nativeStopContainer(
        JNIEnv* /*env*/, jobject /*thiz*/, jint signal, jint timeout_ms) {
    return GuestLauncher::getInstance().stopContainer(signal, timeout_ms)
               ? JNI_TRUE
               : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerNativeBridge_nativeGetContainerState(
        JNIEnv* /*env*/, jobject /*thiz*/) {
    return static_cast<jint>(GuestLauncher::getInstance().state());
}

extern "C" JNIEXPORT void JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerNativeBridge_nativeSetLifecycleListener(
        JNIEnv* env, jobject /*thiz*/, jobject listener) {
    jobject old = g_listener.exchange(nullptr, std::memory_order_acq_rel);
    if (old != nullptr) env->DeleteGlobalRef(old);

    if (listener != nullptr) {
        jobject ref = env->NewGlobalRef(listener);
        g_listener.store(ref, std::memory_order_release);
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerNativeBridge_nativeAttachSurface(
        JNIEnv* env, jobject /*thiz*/, jobject surface) {
    ANativeWindow* window =
        surface != nullptr ? ANativeWindow_fromSurface(env, surface) : nullptr;
    if (window == nullptr) return JNI_FALSE;
    accore::DisplayRenderer::getInstance().setNativeWindow(window);
    ANativeWindow_release(window); /* renderer keeps its own reference */
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerNativeBridge_nativeDetachSurface(
        JNIEnv* /*env*/, jobject /*thiz*/) {
    accore::DisplayRenderer::getInstance().destroyWindow();
}

extern "C" JNIEXPORT jint JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerNativeBridge_nativeSendTouch(
        JNIEnv* /*env*/, jobject /*thiz*/, jint action, jint pointer_id,
        jint x_fixed, jint y_fixed, jint pressure) {
    return static_cast<jint>(accore::InputConsumer::getInstance().dispatchTouch(
        static_cast<uint32_t>(action), static_cast<uint32_t>(pointer_id),
        static_cast<uint32_t>(x_fixed), static_cast<uint32_t>(y_fixed),
        static_cast<uint32_t>(pressure)));
}
