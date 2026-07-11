#pragma once

#include <android/input.h>
#include "imgui/backends/imgui_impl_android.h"
#include "Starcoolxdl/xdl.h"
#include "dlfcn.hpp"
#include "And64InlineHook/And64InlineHook.hpp"
int32_t g_ActivePointerIndex = -1;
bool g_DraggingFromOutsideImGui = false;
int32_t (*origConsume)(void *thiz, void *arg1, bool arg2, long arg3,
                       uint32_t *arg4, AInputEvent **input_event) = nullptr;

void (*origInput)(void *thiz, void *ex_ab, void *ex_ac) = nullptr;
int32_t myConsume(void *thiz, void *arg1, bool arg2, long arg3,
                  uint32_t *arg4, AInputEvent **input_event) {
    int32_t result = origConsume(thiz, arg1, arg2, arg3, arg4, input_event);
    if (result != 0 || !*input_event)
        return result;
    ImGui_ImplAndroid_HandleInputEvent(*input_event);

    return result;
}
void myInput(void *thiz, void *ex_ab, void *ex_ac) {
    origInput(thiz, ex_ab, ex_ac);
    ImGui_ImplAndroid_HandleInputEvent(reinterpret_cast<AInputEvent *>(thiz));
}
// _InitializeMotionEvent is hooked via __INPUT__ if available on device


void __INPUT__() {
    void *handle_input = xdl_open("libinput.so", XDL_DEFAULT);
    void *sym = xdl_sym(handle_input,
                        "_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE",
                        nullptr);

    if (!sym) {
        sym = xdl_sym(handle_input,
                      "_ZN7android13InputConsumer7consumeEPNS_26InputEventFactoryInterfaceEblPjPPNS_10InputEventE",
                      nullptr);
        if (sym) {
            DobbyHook(sym, (void *)myConsume, (void **)&origConsume);
        }
    } else {
        DobbyHook(sym, (void *)myInput, (void **)&origInput);
    }
}
