#include "varn/varn.h"

#include <jni.h>

namespace {

varn_runtime* asRuntime(jlong handle) {
    return reinterpret_cast<varn_runtime*>(handle);
}

} // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_com_varn_VarnRuntime_nativeNew(JNIEnv*, jclass) {
    return reinterpret_cast<jlong>(varn_runtime_new());
}

JNIEXPORT jint JNICALL Java_com_varn_VarnRuntime_nativeRunFile(JNIEnv* env, jclass, jlong handle, jstring path) {
    const char* nativePath = env->GetStringUTFChars(path, nullptr);
    const jint code = varn_runtime_run_file(asRuntime(handle), nativePath);
    env->ReleaseStringUTFChars(path, nativePath);
    return code;
}

JNIEXPORT jint JNICALL Java_com_varn_VarnRuntime_nativeRunString(JNIEnv* env, jclass, jlong handle, jstring source, jstring chunkName) {
    const char* nativeSource = env->GetStringUTFChars(source, nullptr);
    const char* nativeChunk = env->GetStringUTFChars(chunkName, nullptr);
    const jint code = varn_runtime_run_string(asRuntime(handle), nativeSource, nativeChunk);
    env->ReleaseStringUTFChars(source, nativeSource);
    env->ReleaseStringUTFChars(chunkName, nativeChunk);
    return code;
}

JNIEXPORT void JNICALL Java_com_varn_VarnRuntime_nativeStop(JNIEnv*, jclass, jlong handle) {
    varn_runtime_stop(asRuntime(handle));
}

JNIEXPORT void JNICALL Java_com_varn_VarnRuntime_nativeFree(JNIEnv*, jclass, jlong handle) {
    varn_runtime_free(asRuntime(handle));
}

JNIEXPORT jstring JNICALL Java_com_varn_VarnRuntime_nativeVersion(JNIEnv* env, jclass) {
    return env->NewStringUTF(varn_version());
}

} // extern "C"
