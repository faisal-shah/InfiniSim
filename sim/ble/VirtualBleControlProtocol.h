#pragma once

#include "ble/VirtualBleAdapter.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace InfiniSim::Ble {
  class VirtualControlFramer {
  public:
    static constexpr size_t MaximumLineLength = 1024;

    bool Push(std::string_view bytes, std::vector<std::string>& lines);
    void Reset();

  private:
    std::string pending;
  };

  struct VirtualControlCommand {
    enum class Kind {
      Query,
      NextPeer,
      Connect,
      Disconnect,
      ForceConnect,
      ForceDisconnect,
      GapResult,
      Advance,
      Drain,
      StoreFailure,
      PowerCut,
      Cccd,
      Reboot,
      Reset,
    };

    Kind kind = Kind::Query;
    VirtualPeer peer {};
    VirtualBleAdapter::GapOperation gapOperation = VirtualBleAdapter::GapOperation::Start;
    VirtualBleAdapter::InjectedGapResult gapResult {};
    VirtualBleAdapter::StoreFailure storeFailure = VirtualBleAdapter::StoreFailure::None;
    VirtualBleAdapter::PowerCut powerCut = VirtualBleAdapter::PowerCut::None;
    uint32_t value = 0;
    uint16_t handle = 0;
    uint16_t flags = 0;
  };

  std::optional<VirtualControlCommand> ParseVirtualControlCommand(std::string_view line, std::string& error);
}
