#include <csignal>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <cstdio>
#include "FreeRTOS.h"
#include <numeric>
#include <unordered_map>
#include <stdio.h>
#include <stdlib.h>

void NVIC_SystemReset(void) {
}

void APP_ERROR_HANDLER(int err) {
  fprintf(stderr, "APP_ERROR_HANDLER: %d", err);
}

namespace {
  bool heapTrackingAlive = false;

  struct HeapTracking {
    std::unordered_map<void*, size_t> allocatedMemory;
    size_t used = 0;
    size_t currentFreeHeap = configTOTAL_HEAP_SIZE;
    size_t minimumEverFreeHeap = configTOTAL_HEAP_SIZE;

    HeapTracking() {
      heapTrackingAlive = true;
    }

    ~HeapTracking() {
      heapTrackingAlive = false;
    }
  };

  HeapTracking heapTracking;
}

extern "C" int mallocFailedCount;

namespace {
  // The simulator used to hand every request straight to the host allocator, so
  // pvPortMalloc could never fail. That makes a whole class of firmware bug
  // unreproducible here: anything that exhausts the watch's heap simply works
  // in the simulator, and code that checks for a null return is never exercised.
  // The budget is now enforced against configTOTAL_HEAP_SIZE.
  //
  // The simulator also starts far emptier than the watch, which carries the BLE
  // stack and its buffers, so INFINISIM_HEAP_BALLAST reserves bytes up front to
  // reproduce hardware headroom. Read once, on first use.
  size_t HeapBallast() {
    static const size_t ballast = [] {
      const char* env = getenv("INFINISIM_HEAP_BALLAST");
      const size_t v = env ? strtoul(env, nullptr, 10) : 0;
      if (v > 0) {
        fprintf(stderr, "[heap] ballast %zu B reserved; %zu B usable of %d\n", v, configTOTAL_HEAP_SIZE - v, (int) configTOTAL_HEAP_SIZE);
        fflush(stderr);
      }
      return v;
    }();
    return ballast;
  }
}

void* pvPortMalloc(size_t xWantedSize) {
  if (heapTrackingAlive && heapTracking.used + HeapBallast() + xWantedSize > configTOTAL_HEAP_SIZE) {
    mallocFailedCount++;
    return nullptr; // exactly what the watch does when heap_4 cannot satisfy it
  }
  void* ptr = malloc(xWantedSize);
  if (!heapTrackingAlive || ptr == nullptr) {
    return ptr;
  }
  heapTracking.allocatedMemory[ptr] = xWantedSize;
  heapTracking.used += xWantedSize;
  heapTracking.currentFreeHeap = configTOTAL_HEAP_SIZE - heapTracking.used - HeapBallast();
  heapTracking.minimumEverFreeHeap = std::min(heapTracking.currentFreeHeap, heapTracking.minimumEverFreeHeap);

  return ptr;
}

void vPortFree(void* pv) {
  if (heapTrackingAlive) {
    const auto it = heapTracking.allocatedMemory.find(pv);
    if (it != heapTracking.allocatedMemory.end()) {
      heapTracking.used -= it->second;
      heapTracking.currentFreeHeap = configTOTAL_HEAP_SIZE - heapTracking.used - HeapBallast();
      heapTracking.allocatedMemory.erase(it);
    }
  }
  free(pv);
}

size_t xPortGetHeapSize(void) {
  return configTOTAL_HEAP_SIZE;
}

namespace {
  // Heap accounting is faithful in the simulator even though the allocator is
  // not: pvPortMalloc counts every byte. That is enough to measure how close a
  // screen transition comes to exhausting the watch's 39.5 KB heap. Dump with
  // SIGUSR2.
  volatile sig_atomic_t heapDumpRequested = 0;
  void OnHeapSignal(int) { heapDumpRequested = 1; }
  void HeapWatcher() {
    while (true) {
      if (heapDumpRequested) {
        heapDumpRequested = 0;
        fprintf(stderr, "[heapstats] free=%zu min=%zu total=%d\n",
                heapTracking.currentFreeHeap, heapTracking.minimumEverFreeHeap, (int) configTOTAL_HEAP_SIZE);
        fflush(stderr);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  struct HeapDumpInit {
    HeapDumpInit() { std::signal(SIGUSR2, OnHeapSignal); std::thread(HeapWatcher).detach(); }
  } heapDumpInit;
}

size_t xPortGetFreeHeapSize(void) {
  return heapTracking.currentFreeHeap;
}

size_t xPortGetMinimumEverFreeHeapSize(void) {
  return heapTracking.minimumEverFreeHeap;
}
