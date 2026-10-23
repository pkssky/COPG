// Minimal jni.h stub for HOST SYNTAX CHECKING ONLY.
// NOT part of the device build — the NDK provides the real jni.h.
// Only declares what the COPG engine actually uses.
#pragma once

#include <cstdint>

using jint = int32_t;
using jlong = int64_t;
using jboolean = uint8_t;
using jbyte = int8_t;
using jsize = jint;

class _jobject {};
using jobject = _jobject *;
using jstring = jobject;
using jintArray = jobject;
using jobjectArray = jobject;

struct _jclass;
using jclass = _jclass *;

struct JNINativeMethod {
    const char *name;
    const char *signature;
    void *fnPtr;
};

// Only the two methods the engine calls.
struct JNIEnv {
    const char *GetStringUTFChars(jstring str, jboolean *isCopy) { return nullptr; }
    void ReleaseStringUTFChars(jstring str, const char *chars) {}
};
