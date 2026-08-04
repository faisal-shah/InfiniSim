#pragma once

#include <optional>

namespace InfiniSim::Ble {
  class VirtualClientLifecycle {
  public:
    enum class AttachResult {
      Attached,
      Busy,
    };

    AttachResult TryAttach(int client) {
      if (activeClient.has_value()) {
        return AttachResult::Busy;
      }
      activeClient = client;
      return AttachResult::Attached;
    }

    std::optional<int> ForceAttach(int client) {
      const auto replaced = activeClient;
      activeClient = client;
      return replaced;
    }

    bool Detach(int client) {
      if (!activeClient.has_value() || *activeClient != client) {
        return false;
      }
      activeClient.reset();
      return true;
    }

    void Clear() {
      activeClient.reset();
    }

    bool Active() const {
      return activeClient.has_value();
    }

    std::optional<int> ActiveClient() const {
      return activeClient;
    }

  private:
    std::optional<int> activeClient;
  };
}
