#ifndef AC_LOG_H
#define AC_LOG_H

#include <android/log.h>

#define AC_LOG_TAG "ac.fake"

#define AC_LOGD(...) ((void)__android_log_print(ANDROID_LOG_DEBUG, AC_LOG_TAG, __VA_ARGS__))
#define AC_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, AC_LOG_TAG, __VA_ARGS__))
#define AC_LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, AC_LOG_TAG, __VA_ARGS__))
#define AC_LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, AC_LOG_TAG, __VA_ARGS__))

#endif /* AC_LOG_H */
