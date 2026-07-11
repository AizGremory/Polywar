#pragma once

#include <jni.h>
#include <stdint.h>

namespace NativeDebug {

enum class CrashStage : int32_t {
    Startup = 0,
    EglSwapEntered,
    AndroidNewFrame,
    TouchCountCall,
    TouchReadCall,
    TouchApply,
    OpenGlNewFrame,
    ImGuiNewFrame,
    DrawMenu,
    DrawEsp,
    ImGuiRender,
    RenderDrawData,
    SwapOriginal,
    Idle,
};

// Opens a persistent text log inside the injected application's own storage.
// Calling this more than once is safe.
void Initialize(const char *fallback_package_name);

// Uses Context.getExternalFilesDir()/getFilesDir() when an Application object
// is already available. The filesystem-only paths remain as fallbacks.
void InitializeFromJava(JNIEnv *env);

// Records fatal native signals, ARM64 registers and process maps, then chains
// to Android's original crash handler so the normal tombstone is still made.
void InstallCrashHandlers();

void Log(const char *format, ...)
    __attribute__((format(printf, 1, 2)));

void SetStage(CrashStage stage);
void SetTouchState(int32_t count, int32_t phase, float x, float y,
                   bool mouse_down);

const char *GetLogPath();

}  // namespace NativeDebug
