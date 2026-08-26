/*
 * libcontainer_core.so — JNI bridge of the host-side container coordinator.
 *
 * The Kotlin service calls into this library to spawn/stop guest processes,
 * drive IPC, and query engine state. Native subsystems are added phase by
 * phase; this file owns JNI registration glue.
 */
#include <jni.h>
#include <cstring>

#define CORE_VERSION "0.1.0"

extern "C" JNIEXPORT jstring JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerCore_nativeVersion(
        JNIEnv* env, jobject /*thiz*/) {
    return env->NewStringUTF(CORE_VERSION);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_dev_itzkaguya_aospcontainer_core_ContainerCore_nativePing(
        JNIEnv* /*env*/, jobject /*thiz*/) {
    return JNI_TRUE;
}
