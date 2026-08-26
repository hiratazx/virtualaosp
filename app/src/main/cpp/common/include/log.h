#ifndef AC_LOG_H
#define AC_LOG_H

#include <android/log.h>

#ifdef __ANDROID__
#define AC_LOG_PRINT(prio, tag, fmt, ...) __android_log_print(prio, tag, fmt, ##__VA_ARGS__)
#else
/* Host builds (unit tests): route to stderr. */
#include <cstdio>
#define AC_LOG_PRINT(prio, tag, fmt, ...) \
    fprintf(stderr, "[%s] %s: " fmt "\n", tag, #prio, ##__VA_ARGS__)
#endif

#define AC_LOG_TAG "ac.fake"

#define AC_LOGD(...) ((void)AC_LOG_PRINT(ANDROID_LOG_DEBUG, AC_LOG_TAG, __VA_ARGS__))
#define AC_LOGI(...) ((void)AC_LOG_PRINT(ANDROID_LOG_INFO, AC_LOG_TAG, __VA_ARGS__))
#define AC_LOGW(...) ((void)AC_LOG_PRINT(ANDROID_LOG_WARN, AC_LOG_TAG, __VA_ARGS__))
#define AC_LOGE(...) ((void)AC_LOG_PRINT(ANDROID_LOG_ERROR, AC_LOG_TAG, __VA_ARGS__))

#endif /* AC_LOG_H */
