#include "ble/VirtualBleControlServer.h"

#include "components/ble/NimbleController.h"
#include "gatt_bridge.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace {
  const char* BootStateName(Pinetime::Controllers::BondPersistenceCoordinator::BootState state) {
    using BootState = Pinetime::Controllers::BondPersistenceCoordinator::BootState;
    switch (state) {
      case BootState::Unknown:
        return "unknown";
      case BootState::Restored:
        return "restored";
      case BootState::InitializedEmpty:
        return "initialized_empty";
      case BootState::Missing:
        return "missing";
      case BootState::Invalid:
        return "invalid";
      case BootState::RestoreFailed:
        return "restore_failed";
      case BootState::HandshakeFailed:
        return "handshake_failed";
    }
    return "?";
  }

  void AppendPeer(std::ostringstream& output, const Pinetime::Controllers::BondRegistry::PeerIdentity& peer) {
    static constexpr char Hex[] = "0123456789abcdef";
    output << static_cast<unsigned>(peer.type) << ':';
    for (uint8_t byte : peer.address) {
      output << Hex[byte >> 4] << Hex[byte & 0x0f];
    }
  }
}

namespace InfiniSim::Ble {
  VirtualBleControlServer::VirtualBleControlServer(Pinetime::Controllers::NimbleController& nimbleController, GattBridge& gattBridge)
    : nimbleController {nimbleController}, gattBridge {gattBridge} {
  }

  VirtualBleControlServer::~VirtualBleControlServer() {
    CloseClient();
    if (listenFd >= 0) {
      close(listenFd);
    }
  }

  bool VirtualBleControlServer::Start(uint16_t port) {
    listenFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listenFd < 0) {
      perror("ble-control socket");
      return false;
    }
    const int one = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (bind(listenFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(listenFd, 1) != 0) {
      perror("ble-control bind/listen");
      close(listenFd);
      listenFd = -1;
      return false;
    }
    std::printf("InfiniSim: virtual BLE test control listening on 127.0.0.1:%u\n", port);
    return true;
  }

  void VirtualBleControlServer::CloseClient() {
    if (clientFd >= 0) {
      close(clientFd);
      clientFd = -1;
      framer.Reset();
    }
  }

  void VirtualBleControlServer::Poll() {
    if (listenFd < 0) {
      return;
    }
    for (;;) {
      const int incoming = accept4(listenFd, nullptr, nullptr, SOCK_NONBLOCK);
      if (incoming < 0) {
        break;
      }
      if (clientFd >= 0) {
        static constexpr char Busy[] = "ERR control_busy\n";
        send(incoming, Busy, sizeof(Busy) - 1, MSG_NOSIGNAL);
        close(incoming);
      } else {
        clientFd = incoming;
      }
    }
    if (clientFd < 0) {
      return;
    }

    char buffer[1024];
    for (;;) {
      const ssize_t count = read(clientFd, buffer, sizeof(buffer));
      if (count > 0) {
        std::vector<std::string> lines;
        if (!framer.Push(std::string_view(buffer, static_cast<size_t>(count)), lines)) {
          SendLine("ERR frame_too_long");
          CloseClient();
          return;
        }
        for (const auto& line : lines) {
          HandleLine(line);
          if (clientFd < 0) {
            return;
          }
        }
        continue;
      }
      if (count == 0) {
        CloseClient();
      }
      return;
    }
  }

  void VirtualBleControlServer::HandleLine(const std::string& line) {
    std::string error;
    const auto command = ParseVirtualControlCommand(line, error);
    if (!command.has_value()) {
      SendLine("ERR " + error);
      return;
    }
    SendLine(Execute(*command));
  }

  std::string VirtualBleControlServer::Execute(const VirtualControlCommand& command) {
    using Kind = VirtualControlCommand::Kind;
    auto& virtualBle = nimbleController.VirtualBle();
    switch (command.kind) {
      case Kind::Query:
        return Query();
      case Kind::NextPeer:
        virtualBle.SetNextPeer(command.peer);
        return "OK next_peer";
      case Kind::Connect: {
        const auto result = nimbleController.AttachVirtualLink();
        return result == VirtualBleAdapter::AttachResult::Attached ? "OK connected" : "ERR link_busy_or_rejected";
      }
      case Kind::Disconnect:
        if (gattBridge.HasClient()) {
          gattBridge.ForceDisconnectClient();
        } else {
          nimbleController.DetachVirtualLink();
        }
        return "OK disconnected";
      case Kind::ForceConnect:
        gattBridge.ForceDisconnectClient();
        return nimbleController.ForceAttachVirtualLink() == VirtualBleAdapter::AttachResult::Attached ? "OK force_connected"
                                                                                                      : "ERR peer_rejected";
      case Kind::ForceDisconnect:
        gattBridge.ForceDisconnectClient();
        nimbleController.DetachVirtualLink();
        return "OK force_disconnected";
      case Kind::GapResult:
        virtualBle.InjectGapResult(command.gapOperation, command.gapResult);
        nimbleController.DrainVirtualPolicy();
        return "OK gap_result_injected";
      case Kind::Advance:
        nimbleController.AdvanceVirtualPolicyTime(command.value);
        return "OK advanced";
      case Kind::Drain:
        nimbleController.DrainVirtualPolicy();
        return "OK drained";
      case Kind::StoreFailure:
        virtualBle.SetStoreFailure(command.storeFailure);
        return "OK store_failure";
      case Kind::PowerCut:
        virtualBle.SetPowerCut(command.powerCut);
        return "OK power_cut";
      case Kind::Cccd:
        return virtualBle.WriteActivePeerCccd(command.handle, command.flags) ? "OK cccd" : "ERR cccd_rejected";
      case Kind::Reboot:
        gattBridge.ForceDisconnectClient();
        nimbleController.RebootVirtualBle();
        return "OK rebooted";
      case Kind::Reset:
        gattBridge.ForceDisconnectClient();
        nimbleController.ResetVirtualBle(true);
        return "OK reset";
    }
    return "ERR unsupported";
  }

  std::string VirtualBleControlServer::Query() const {
    const auto& virtualBle = nimbleController.VirtualBle();
    const auto state = virtualBle.Query();
    std::ostringstream output;
    output << "OK"
           << " radio_desired=" << VirtualBleAdapter::Radio::ToString(state.desired)
           << " radio_actual=" << VirtualBleAdapter::Radio::ToString(state.actual)
           << " virtual_gap_active=" << (state.virtualAdvertisingCommandActive ? 1 : 0) << " link=" << (state.connected ? 1 : 0)
           << " bonded=" << (state.bonded ? 1 : 0) << " authenticated=" << (state.authenticated ? 1 : 0)
           << " persistence_writes_enabled=" << (state.persistenceWritesEnabled ? 1 : 0)
           << " retained_peers=" << static_cast<unsigned>(state.retainedPeers) << " retained=";
    for (uint8_t index = 0; index < state.retainedPeers; index++) {
      if (index != 0) {
        output << ',';
      }
      AppendPeer(output, virtualBle.RetainedPeer(index).peer);
    }
    output << " cccds=" << static_cast<unsigned>(state.cccdCount) << " evictions=" << state.evictions << " now_ms=" << state.nowMs
           << " gap_starts=" << state.counters.gapStarts << " gap_stops=" << state.counters.gapStops
           << " gap_terminates=" << state.counters.gapTerminates << " host_policy_events=" << state.counters.hostPolicyEvents
           << " flash_writes=" << state.persistence.flashWriteCount << " flash_bytes=" << state.persistence.flashBytes
           << " critical_dirty=" << (state.persistence.criticalDirty ? 1 : 0) << " usage_dirty=" << (state.persistence.usageDirty ? 1 : 0)
           << " persistence_pending=" << (state.persistence.pending ? 1 : 0)
           << " persistence_inflight=" << (state.persistence.inFlight ? 1 : 0) << " write_failures=" << state.persistence.writeFailures
           << " decode_failures=" << state.persistence.decodeFailures
           << " persistence_write_attempts=" << state.counters.persistenceWriteAttempts
           << " persistence_wakelock_ms=" << state.counters.persistenceWakeLockDurationMs
           << " boot=" << BootStateName(state.persistence.bootState) << " store_failure=" << VirtualBleAdapter::ToString(state.storeFailure)
           << " power_cut=" << VirtualBleAdapter::ToString(state.powerCut);
    return output.str();
  }

  void VirtualBleControlServer::SendLine(const std::string& line) {
    if (clientFd < 0) {
      return;
    }
    const std::string framed = line + "\n";
    if (send(clientFd, framed.data(), framed.size(), MSG_NOSIGNAL) < 0) {
      CloseClient();
    }
  }
}
