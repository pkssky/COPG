// COPG open-source engine — logging helpers.
// All engine logs go to logcat with tag "COPGModule" (the WebUI console greps
// `logcat -s COPGModule`), so keep the tag stable.
#pragma once

#include <android/log.h>

#define COPG_LOG_TAG "COPGModule"

#ifdef COPG_QUIET
#define COPG_LOGD(...) ((void) 0)
#define COPG_LOGI(...) ((void) 0)
#define COPG_LOGW(...) ((void) 0)
#define COPG_LOGE(...) ((void) 0)
#else
#define COPG_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, COPG_LOG_TAG, __VA_ARGS__)
#define COPG_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  COPG_LOG_TAG, __VA_ARGS__)
#define COPG_LOGW(...) __android_log_print(ANDROID_LOG_WARN,  COPG_LOG_TAG, __VA_ARGS__)
#define COPG_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, COPG_LOG_TAG, __VA_ARGS__)
#endif
