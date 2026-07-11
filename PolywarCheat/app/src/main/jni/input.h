#pragma once

#include <pthread.h>

// ImGui is updated from the EGL render thread. Keep a mutex available for
// future producers, but do not hook Android's private libinput C++ ABI: its
// signatures vary between Android/vendor builds and can crash on the first
// MotionEvent. Touch state is polled from UnityEngine.Input in Main.cpp.
pthread_mutex_t g_ImGuiMutex = PTHREAD_MUTEX_INITIALIZER;
