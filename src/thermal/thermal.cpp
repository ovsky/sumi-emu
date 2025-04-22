// app/src/main/cpp/core/thermal.cpp
#include "thermal.h"
#include <jni.h>

namespace Core {
    static std::atomic<float> g_throttle_factor{1.0f};

    void SetThrottleFactor(float factor) {
        g_throttle_factor.store(factor);
    }

    float GetThrottleFactor() {
        return g_throttle_factor.load();
    }
}

// JNI Interface
extern "C" JNIEXPORT void JNICALL
Java_org_sumi_emu_NativeLibrary_setThrottleFactor(JNIEnv* env, jclass clazz, jfloat factor) {
    Core::SetThrottleFactor(factor);
}