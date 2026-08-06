#include "semphr.h"
#include <SDL.h>
#include <mutex>
#include <chrono>
#include <stdexcept>

QueueHandle_t xSemaphoreCreateMutex() {
  SemaphoreHandle_t xSemaphore = xQueueCreate(1, 1);
  Queue_t* pxQueue = (Queue_t*) xSemaphore;
  // Queue full represents taken semaphore/locked mutex
  pxQueue->queue.push_back(0);
  return xSemaphore;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait) {
  Queue_t* pxQueue = (Queue_t*) xSemaphore;
  std::unique_lock<std::mutex> lock(pxQueue->mutex);
  const auto available = [pxQueue] { return pxQueue->queue.empty(); };
  if (xTicksToWait == portMAX_DELAY) {
    pxQueue->condition.wait(lock, available);
  } else if (!pxQueue->condition.wait_for(
               lock,
               std::chrono::milliseconds(xTicksToWait),
               available)) {
    return pdFALSE;
  }
  pxQueue->queue.push_back(0);
  return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore) {
  Queue_t* pxQueue = (Queue_t*) xSemaphore;
  {
    std::lock_guard<std::mutex> guard(pxQueue->mutex);
    if (pxQueue->queue.size() != 1) {
      throw std::runtime_error("Mutex released without being held");
    }
    pxQueue->queue.pop_back();
  }
  pxQueue->condition.notify_one();
  return pdTRUE;
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
  // Only ever locked with portMAX_DELAY, so std::recursive_mutex maps directly.
  return reinterpret_cast<SemaphoreHandle_t>(new std::recursive_mutex());
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t xSemaphore, TickType_t /*xTicksToWait*/) {
  reinterpret_cast<std::recursive_mutex*>(xSemaphore)->lock();
  return true;
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t xSemaphore) {
  reinterpret_cast<std::recursive_mutex*>(xSemaphore)->unlock();
  return true;
}
