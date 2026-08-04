#pragma once

#include "ble/VirtualBleControlProtocol.h"

#include <cstdint>

class GattBridge;

namespace Pinetime::Controllers {
  class NimbleController;
}

namespace InfiniSim::Ble {
  class VirtualBleControlServer {
  public:
    VirtualBleControlServer(Pinetime::Controllers::NimbleController& nimbleController, GattBridge& gattBridge);
    ~VirtualBleControlServer();

    bool Start(uint16_t port);
    void Poll();

  private:
    void CloseClient();
    void HandleLine(const std::string& line);
    std::string Execute(const VirtualControlCommand& command);
    std::string Query() const;
    void SendLine(const std::string& line);

    Pinetime::Controllers::NimbleController& nimbleController;
    GattBridge& gattBridge;
    VirtualControlFramer framer;
    int listenFd = -1;
    int clientFd = -1;
  };
}
