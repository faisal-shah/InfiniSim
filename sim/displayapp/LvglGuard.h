#pragma once

// Shadows InfiniTime's no-op displayapp/LvglGuard.h (the sim include path wins).
//
// LVGL is not thread-safe, and unlike the watch the simulator has TWO threads
// driving it: the DisplayApp task, and the SDL main thread which renders the
// "Screen is OFF" overlay (and runs lv_task_handler while the display task
// sleeps). The wake/sleep handoff between them is a use-after-free factory —
// AddressSanitizer showed main-thread lv_obj_del(screen_off_bg) racing the
// display task's lv_task_handler. This mutex serializes both sides.

#include <mutex>

namespace Pinetime {
  namespace Sim {
    extern std::recursive_mutex lvglMutex;
  }
}

#define LVGL_GUARD() std::lock_guard<std::recursive_mutex> lvglGuardLock(Pinetime::Sim::lvglMutex)
