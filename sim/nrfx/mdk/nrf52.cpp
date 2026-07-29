#include "mdk/nrf52.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

// Watchdog emulation.
//
// The firmware reloads the watchdog by writing 0x6E524635 to RR[0]; on real
// hardware, going longer than CRV ticks without that write reboots the watch
// with reset reason "wdg". The simulator used to model NRF_WDT as an inert
// struct, so watchdog starvation -- a whole class of field failure -- was
// invisible here. This polls RR[0], consumes each reload, and shouts when the
// deadline passes.
namespace {
  constexpr uint32_t ReloadValue = 0x6E524635UL;
  std::atomic<bool> watchdogMonitorRunning {false};

  void WatchdogMonitor() {
    using clock = std::chrono::steady_clock;
    auto lastReload = clock::now();
    bool starved = false;
    while (watchdogMonitorRunning) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      if (NRF_WDT == nullptr) {
        continue;
      }
      if (NRF_WDT->RR[0] == ReloadValue) {
        NRF_WDT->RR[0] = 0; // consume it, so the next write is observable
        lastReload = clock::now();
        if (starved) {
          fprintf(stderr, "[watchdog] fed again after starving\n");
          fflush(stderr);
          starved = false;
        }
        continue;
      }
      // Only armed once the firmware has set a counter reload value.
      if (NRF_WDT->CRV == 0) {
        lastReload = clock::now();
        continue;
      }
      static bool armedReported = false;
      if (!armedReported) {
        armedReported = true;
        fprintf(stderr, "[watchdog] armed: CRV=%u -> timeout %.1fs\n", NRF_WDT->CRV, (NRF_WDT->CRV + 1.0) / 32768.0);
        fflush(stderr);
      }
      const double timeoutSeconds = (static_cast<double>(NRF_WDT->CRV) + 1.0) / 32768.0;
      const double since = std::chrono::duration<double>(clock::now() - lastReload).count();
      if (!starved && since > timeoutSeconds) {
        starved = true;
        fprintf(stderr,
                "[watchdog] STARVED: no reload for %.1fs (timeout %.1fs) -- hardware would reboot with reason wdg\n",
                since,
                timeoutSeconds);
        fflush(stderr);
      }
    }
  }
}

void start_watchdog_monitor() {
  if (!watchdogMonitorRunning.exchange(true)) {
    std::thread(WatchdogMonitor).detach();
  }
}

// pointer variable used by the rest of the code, and its initialization
NRF_WDT_Type *NRF_WDT;
void init_NRF_WDT()
{
  static NRF_WDT_Type NRF_WDT_object;
  NRF_WDT = &NRF_WDT_object;
  start_watchdog_monitor();
}

// pointer variable used by the rest of the code, and its initialization
NRF_POWER_Type *NRF_POWER;
void init_NRF_POWER()
{
  static NRF_POWER_Type NRF_POWER_object;
  NRF_POWER = &NRF_POWER_object;
  // sim: always return ResetPin as reason for reboot
  NRF_POWER->RESETREAS = 0x01;
}
