#include "notify_queue.h"

#include <deque>
#include <mutex>

namespace {
  struct Entry {
    uint8_t charId;
    std::vector<uint8_t> bytes;
  };
  std::mutex mtx;
  std::deque<Entry> queue;
  uint8_t activeCharId = 0;
}

namespace SimNotify {
  void SetActiveCharId(uint8_t charId) {
    std::lock_guard<std::mutex> lock(mtx);
    activeCharId = charId;
  }

  void Push(const uint8_t* data, uint16_t len) {
    std::lock_guard<std::mutex> lock(mtx);
    queue.push_back({activeCharId, std::vector<uint8_t>(data, data + len)});
  }

  bool Pop(uint8_t& charId, std::vector<uint8_t>& bytes) {
    std::lock_guard<std::mutex> lock(mtx);
    if (queue.empty()) {
      return false;
    }
    charId = queue.front().charId;
    bytes = std::move(queue.front().bytes);
    queue.pop_front();
    return true;
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mtx);
    queue.clear();
  }
}
