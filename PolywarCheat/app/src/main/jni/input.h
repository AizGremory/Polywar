#pragma once

#include <android/input.h>
#include "imgui/backends/imgui_impl_android.h"
#include "Starcoolxdl/xdl.h"
#include "dlfcn.hpp"
#include "And64InlineHook/And64InlineHook.hpp"
#include <pthread.h>

int32_t g_ActivePointerIndex = -1;
bool g_DraggingFromOutsideImGui = false;
pthread_mutex_t g_ImGuiMutex = PTHREAD_MUTEX_INITIALIZER;

int32_t (*origConsume)(void *thiz, void *arg1, bool arg2, long arg3,
                       uint32_t *arg4, AInputEvent **input_event) = nullptr;

void (*origInput)(void *thiz, void *ex_ab, void *ex_ac) = nullptr;

static void HandleImGuiInput(AInputEvent *event) {
    if (!event || ImGui::GetCurrentContext() == nullptr)
        return;

    pthread_mutex_lock(&g_ImGuiMutex);
    if (ImGui::GetCurrentContext() != nullptr)
        ImGui_ImplAndroid_HandleInputEvent(event);
    pthread_mutex_unlock(&g_ImGuiMutex);
}

int32_t myConsume(void *thiz, void *arg1, bool arg2, long arg3,
                  uint32_t *arg4, AInputEvent **input_event) {
    int32_t result = origConsume ? origConsume(thiz, arg1, arg2, arg3, arg4, input_event) : 0;
    if (result != 0 || !input_event || !*input_event)
        return result;
    HandleImGuiInput(*input_event);

    return result;
}
void myInput(void *thiz, void *ex_ab, void *ex_ac) {
    if (origInput)
        origInput(thiz, ex_ab, ex_ac);

    // initializeMotionEvent(this, MotionEvent*, InputMessage*) receives the
    // actual AInputEvent-compatible MotionEvent in its second argument.
    HandleImGuiInput(reinterpret_cast<AInputEvent *>(ex_ab));
}

bool __INPUT__() {
    void *handle_input = xdl_open("libinput.so", XDL_DEFAULT);
    if (!handle_input) {
        LOGW("Input hook skipped: libinput.so is unavailable");
        return false;
    }

    void *sym = xdl_sym(handle_input,
                        "_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE",
                        nullptr);

    if (sym && DobbyHook(sym, (void *)myInput, (void **)&origInput) == RT_SUCCESS && origInput) {
        LOGI("Input hook installed through initializeMotionEvent");
        return true;
    }

    sym = xdl_sym(handle_input,
                  "_ZN7android13InputConsumer7consumeEPNS_26InputEventFactoryInterfaceEblPjPPNS_10InputEventE",
                  nullptr);
    if (sym && DobbyHook(sym, (void *)myConsume, (void **)&origConsume) == RT_SUCCESS && origConsume) {
        LOGI("Input hook installed through InputConsumer::consume");
        return true;
    }

    LOGW("Input hook skipped: no compatible libinput symbol was found");
    return false;
}
