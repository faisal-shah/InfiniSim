#include "ble/VirtualBleAdapter.h"
#include "ble/VirtualBleControlProtocol.h"
#include "ble/VirtualClientLifecycle.h"
#include "ble/VirtualGattAccessPolicy.h"
#include "ble/VirtualRadioIntent.h"
#include "components/ble/BondStoreCodec.h"
#include "components/ble/CompanionManagementStatus.h"
#include "generated/CompanionProtocol.h"
#include "host/ble_att.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
  int checks = 0;
  int failures = 0;

  void Check(bool condition, const char* description) {
    checks++;
    if (!condition) {
      failures++;
      std::printf("FAIL: %s\n", description);
    }
  }

  InfiniSim::Ble::VirtualPeer
  Peer(uint8_t value, InfiniSim::Ble::VirtualSecurity security = InfiniSim::Ble::VirtualSecurity::Authenticated, bool replace = false) {
    return {{1, {value, static_cast<uint8_t>(value + 1), 2, 3, 4, 5}}, security, replace};
  }

  void ConnectAndDisconnect(InfiniSim::Ble::VirtualBleAdapter& adapter, const InfiniSim::Ble::VirtualPeer& peer) {
    adapter.SetNextPeer(peer);
    char description[64];
    std::snprintf(description, sizeof(description), "virtual peer %u attaches", peer.identity.address[0]);
    Check(adapter.ConnectNextPeer() == InfiniSim::Ble::VirtualBleAdapter::AttachResult::Attached, description);
    adapter.Disconnect();
    adapter.AdvanceTime(Pinetime::Controllers::BondPersistenceCoordinator::CriticalSettleMs);
  }

  std::vector<uint8_t> ReadBytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }

  void WriteBytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }

  bool DecodeFile(const std::string& path, Pinetime::Controllers::NimbleBondStoreSnapshot& snapshot) {
    const auto bytes = ReadBytes(path);
    const auto decoded = Pinetime::Controllers::BondStoreCodec::Decode(bytes.data(), bytes.size(), snapshot);
    return decoded && decoded.formatInitialized;
  }
}

int main() {
  using InfiniSim::Ble::VirtualBleAdapter;
  using InfiniSim::Ble::VirtualClientLifecycle;
  using InfiniSim::Ble::VirtualControlCommand;
  using InfiniSim::Ble::VirtualControlFramer;
  using InfiniSim::Ble::VirtualGattAccessPolicy;

  {
    using DesiredMode = VirtualBleAdapter::Radio::DesiredMode;
    Check(InfiniSim::Ble::DesiredModeForBeaconRequest(true, false) == DesiredMode::Beacon,
          "beacon enable requests beacon mode even when connectable radio is disabled");
    Check(InfiniSim::Ble::DesiredModeForBeaconRequest(false, true) == DesiredMode::Connectable,
          "beacon disable restores connectable mode when the radio is enabled");
    Check(InfiniSim::Ble::DesiredModeForBeaconRequest(false, false) == DesiredMode::Off, "beacon disable preserves radio-off intent");
  }

  {
    VirtualClientLifecycle lifecycle;
    Check(lifecycle.TryAttach(10) == VirtualClientLifecycle::AttachResult::Attached, "first client obtains the single active lease");
    Check(lifecycle.TryAttach(11) == VirtualClientLifecycle::AttachResult::Busy, "second client is busy without replacing the incumbent");
    Check(lifecycle.ActiveClient() == 10, "busy attach preserves the incumbent");
    Check(lifecycle.ForceAttach(11) == 10 && lifecycle.ActiveClient() == 11, "explicit force attach reports and replaces the incumbent");
    Check(lifecycle.Detach(11) && !lifecycle.Active(), "active client detaches through shared lifecycle");
  }

  {
    Check(VirtualGattAccessPolicy::Authorize(static_cast<uint8_t>(SimCompanionProtocol::BridgeChar::CompanionStatus), 1, false) == 0,
          "public companion status is readable without authentication");
    Check(VirtualGattAccessPolicy::Authorize(static_cast<uint8_t>(SimCompanionProtocol::BridgeChar::CompanionVerify), 1, false) ==
            BLE_ATT_ERR_INSUFFICIENT_AUTHEN,
          "companion verify returns the ATT authentication error on an unauthenticated link");
    Check(VirtualGattAccessPolicy::Authorize(static_cast<uint8_t>(SimCompanionProtocol::BridgeChar::ScheduleSync), 0, false) ==
            BLE_ATT_ERR_INSUFFICIENT_AUTHEN,
          "generated metadata protects schedule writes");
    Check(VirtualGattAccessPolicy::Authorize(static_cast<uint8_t>(SimCompanionProtocol::BridgeChar::Battery), 1, false) == 0,
          "generated metadata leaves battery reads public");
    Check(VirtualGattAccessPolicy::Authorize(static_cast<uint8_t>(SimCompanionProtocol::BridgeChar::CompanionVerify), 1, true) == 0,
          "authenticated virtual security permits companion verify");
  }

  {
    VirtualControlFramer framer;
    std::vector<std::string> lines;
    Check(framer.Push("QUERY\nNEXT_", lines), "partial control frame is accepted");
    Check(lines.size() == 1 && lines[0] == "QUERY", "complete control frame drains immediately");
    Check(framer.Push("PEER 1 010203040506 AUTHENTICATED\n", lines), "remaining control frame is accepted");
    Check(lines.size() == 2 && lines[1].starts_with("NEXT_PEER"), "fragmented command is reassembled");

    std::string error;
    const auto next = InfiniSim::Ble::ParseVirtualControlCommand(lines[1], error);
    Check(next.has_value() && next->kind == VirtualControlCommand::Kind::NextPeer &&
            next->peer.security == InfiniSim::Ble::VirtualSecurity::Authenticated,
          "NEXT_PEER control syntax parses");
    const auto gap = InfiniSim::Ble::ParseVirtualControlCommand("GAP_RESULT START -7 FAILED", error);
    Check(gap.has_value() && gap->gapResult.code == -7 && gap->gapResult.classification == VirtualBleAdapter::Radio::Result::Failed,
          "normalized GAP result injection parses");
    const auto cccd = InfiniSim::Ble::ParseVirtualControlCommand("CCCD 0x0100 0x0081", error);
    Check(cccd.has_value() && cccd->handle == 0x0100 && cccd->flags == 0x0081, "CCCD control syntax accepts explicit hexadecimal values");
  }

  {
    const std::string path = "infinisim-ble-initialization-failure-store.bin";
    std::filesystem::remove(path);
    std::filesystem::remove(path + ".next");
    VirtualBleAdapter adapter(path);
    adapter.SetStoreFailure(VirtualBleAdapter::StoreFailure::Write);
    adapter.Initialize();
    Check(adapter.Query().persistence.bootState == Pinetime::Controllers::BondPersistenceCoordinator::BootState::InitializingEmpty,
          "failed first-format write leaves initialization visible");
    Check((adapter.CompanionStatus().flags & Pinetime::Controllers::CompanionStatusFlag::FormatInitializationPending) != 0,
          "failed first-format write exposes the format gate");
    Check(adapter.Query().actual == VirtualBleAdapter::Radio::Mode::Off &&
            adapter.Query().counters.gapStarts == 0,
          "advertising stays off until first-format durability");
    Check(adapter.ConnectNextPeer() == VirtualBleAdapter::AttachResult::Rejected,
          "virtual peers cannot bypass the first-format radio gate");
    adapter.SetStoreFailure(VirtualBleAdapter::StoreFailure::None);
    adapter.AdvanceTime(Pinetime::Controllers::BondPersistenceCoordinator::FailureRetryBaseMs);
    Check(adapter.Query().persistence.bootState == Pinetime::Controllers::BondPersistenceCoordinator::BootState::InitializedEmpty &&
            adapter.Query().actual == VirtualBleAdapter::Radio::Mode::FastConnectable,
          "successful retry releases fast advertising");
    adapter.Reset(true);
  }

  {
    const std::string path = "infinisim-ble-radio-test-store.bin";
    VirtualBleAdapter adapter(path);
    adapter.Reset(true);
    Pinetime::Controllers::NimbleBondStoreSnapshot initialized;
    Check(adapter.Query().persistence.bootState == Pinetime::Controllers::BondPersistenceCoordinator::BootState::InitializedEmpty &&
            adapter.CompanionStatus().resetEpoch == 1 && adapter.Query().persistence.storeGeneration == 1,
          "missing persistence initializes an advanced empty firmware snapshot");
    Check((adapter.CompanionStatus().flags & Pinetime::Controllers::CompanionStatusFlag::LegacyResetThisBoot) == 0,
          "fresh missing persistence does not claim a legacy reset");
    Check(adapter.Query().persistence.flashWriteCount == 1 && DecodeFile(path, initialized),
          "missing persistence is atomically committed with the migration marker");
    Check(adapter.Query().actual == VirtualBleAdapter::Radio::Mode::FastConnectable,
          "real radio state machine starts the virtual command port in fast connectable mode");
    adapter.SetDesiredMode(VirtualBleAdapter::Radio::DesiredMode::Off);
    adapter.InjectGapResult(VirtualBleAdapter::GapOperation::Start, {-23, VirtualBleAdapter::Radio::Result::Failed});
    adapter.SetDesiredMode(VirtualBleAdapter::Radio::DesiredMode::Connectable);
    Check(adapter.Query().retryCount == 1 && adapter.Query().lastStartResult == -23,
          "scripted start failure is classified by the real radio state machine");
    adapter.AdvanceTime(250);
    Check(adapter.Query().actual == VirtualBleAdapter::Radio::Mode::FastConnectable, "virtual time releases the deterministic retry");
    Check(adapter.Query().counters.gapStarts >= 3 && adapter.Query().counters.gapStops >= 1,
          "GAP command counters record starts and stops");
    adapter.Reset(true);
  }

  {
    const std::string path = "infinisim-ble-pre-marker-test-store.bin";
    std::filesystem::remove(path);
    std::filesystem::remove(path + ".next");
    Pinetime::Controllers::NimbleBondStoreSnapshot previous;
    previous.registry.resetEpoch = 4;
    previous.generation = 7;
    Pinetime::Controllers::BondStoreCodec::Buffer encoded {};
    size_t encodedSize = 0;
    Check(Pinetime::Controllers::BondStoreCodec::Encode(previous, encoded, encodedSize, false), "valid pre-marker snapshot encodes");
    WriteBytes(path, std::vector<uint8_t>(encoded.begin(), encoded.begin() + encodedSize));

    VirtualBleAdapter adapter(path);
    adapter.Initialize();
    Check(adapter.Query().persistence.bootState == Pinetime::Controllers::BondPersistenceCoordinator::BootState::InitializedEmpty &&
            adapter.CompanionStatus().resetEpoch == 5 && adapter.Query().persistence.storeGeneration == 8,
          "valid pre-marker persistence is discarded into an advanced empty snapshot");
    Check((adapter.CompanionStatus().flags & Pinetime::Controllers::CompanionStatusFlag::LegacyResetThisBoot) != 0,
          "discarded pre-marker persistence reports the legacy-reset flag");
    Pinetime::Controllers::NimbleBondStoreSnapshot migrated;
    Check(DecodeFile(path, migrated) && migrated.registry.resetEpoch == 5 && migrated.generation == 8,
          "pre-marker replacement is migration-complete and preserves advanced counters");
    adapter.Reset(true);
  }

  {
    const std::string path = "infinisim-ble-persistence-test-store.bin";
    std::filesystem::remove(path);
    std::filesystem::remove(path + ".next");
    VirtualBleAdapter adapter(path);
    adapter.Initialize();
    for (uint8_t peer = 1; peer <= 5; peer++) {
      ConnectAndDisconnect(adapter, Peer(peer));
    }
    Check(adapter.Query().retainedPeers == 5, "five opaque deterministic bond fixtures are retained");
    Check(adapter.Query().persistence.flashWriteCount >= 5, "real coordinator writes settled bond snapshots");
    std::array<uint8_t, Pinetime::Controllers::CompanionProtocol::CompanionManagementStatusSize> managementPayload {};
    Pinetime::Controllers::EncodeCompanionStatus(adapter.CompanionStatus(), managementPayload);
    Check(managementPayload[0] == Pinetime::Controllers::CompanionProtocol::CompanionManagementProtocolVersion &&
            managementPayload[1] == Pinetime::Controllers::CompanionProtocol::RetainedPeers && managementPayload[2] == 5,
          "real companion-management status encoder reports the retained bond count");

    ConnectAndDisconnect(adapter, Peer(1));
    ConnectAndDisconnect(adapter, Peer(6));
    Check(adapter.Query().retainedPeers == 5 && adapter.Query().evictions == 1, "sixth peer follows the real least-recently-used policy");
    Check(adapter.RetainsPeer(Peer(1).identity) && adapter.RetainsPeer(Peer(6).identity) && !adapter.RetainsPeer(Peer(2).identity),
          "recent peer and newcomer survive while the least-recent peer is evicted");

    ConnectAndDisconnect(adapter, Peer(1, InfiniSim::Ble::VirtualSecurity::Authenticated, true));
    Check(adapter.Query().retainedPeers == 5 && adapter.Query().evictions == 1,
          "repeat replacement forgets and re-admits without a second eviction");

    adapter.SetNextPeer(Peer(1));
    Check(adapter.ConnectNextPeer() == VirtualBleAdapter::AttachResult::Attached, "retained peer reconnects for CCCD-only mutation");
    Check(adapter.WriteActivePeerCccd(0x0100, 0x0081), "CCCD-only change is accepted for a bonded fixture");
    adapter.Disconnect();
    adapter.AdvanceTime(Pinetime::Controllers::BondPersistenceCoordinator::CriticalSettleMs);
    Check(adapter.Query().cccdCount == 1, "CCCD-only change reaches the persisted snapshot");

    const auto flashWrites = adapter.Query().persistence.flashWriteCount;
    const auto flashBytes = adapter.Query().persistence.flashBytes;
    const auto wakeProxy = adapter.Query().counters.persistenceWakeLockDurationMs;
    adapter.Reboot();
    Check(adapter.Query().persistence.bootState == Pinetime::Controllers::BondPersistenceCoordinator::BootState::Restored,
          "reboot restores through the real codec");
    Check(adapter.Query().retainedPeers == 5 && adapter.Query().cccdCount == 1, "reboot restores five peers and CCCD records");
    Check(flashWrites > 0 && flashBytes > 0 && wakeProxy > 0, "write, byte, and persistence wake-lock proxies are non-zero after writes");

    adapter.Reset(true);
  }

  {
    const std::string path = "infinisim-ble-power-cut-test-store.bin";
    constexpr std::array cuts {
      VirtualBleAdapter::PowerCut::BeforeReplace,
      VirtualBleAdapter::PowerCut::AfterPartialStagedWrite,
      VirtualBleAdapter::PowerCut::AfterStagedWrite,
    };
    for (const auto cut : cuts) {
      VirtualBleAdapter adapter(path);
      adapter.Reset(true);
      ConnectAndDisconnect(adapter, Peer(1));
      const auto oldLive = ReadBytes(path);
      Pinetime::Controllers::NimbleBondStoreSnapshot oldSnapshot;
      Check(DecodeFile(path, oldSnapshot), "power-cut baseline is a complete migration snapshot");

      adapter.SetPowerCut(cut);
      ConnectAndDisconnect(adapter, Peer(2));
      Check(adapter.Query().persistence.writeFailures == 1, "named atomic power cut fails one replacement attempt");
      Check(ReadBytes(path) == oldLive, "modeled atomic power cut leaves the old live file byte-for-byte intact");

      Pinetime::Controllers::NimbleBondStoreSnapshot liveAfterCut;
      Check(DecodeFile(path, liveAfterCut), "live file remains a complete decodable snapshot after the power cut");
      adapter.SetPowerCut(VirtualBleAdapter::PowerCut::None);
      adapter.Reboot();
      Check(adapter.Query().persistence.bootState == Pinetime::Controllers::BondPersistenceCoordinator::BootState::Restored &&
              adapter.RetainsPeer(Peer(1).identity) && !adapter.RetainsPeer(Peer(2).identity),
            "reboot restores the old complete snapshot after the interrupted replacement");
      adapter.Reset(true);
    }
  }

  {
    const std::string path = "infinisim-ble-corrupt-live-test-store.bin";
    VirtualBleAdapter adapter(path);
    adapter.Reset(true);
    ConnectAndDisconnect(adapter, Peer(1));
    auto corrupt = ReadBytes(path);
    corrupt[Pinetime::Controllers::BondStoreCodec::HeaderSize] ^= 0x80;
    WriteBytes(path, corrupt);

    adapter.Reboot();
    Check(adapter.Query().persistence.bootState == Pinetime::Controllers::BondPersistenceCoordinator::BootState::Invalid &&
            !adapter.Query().persistenceWritesEnabled &&
            (adapter.CompanionStatus().flags & Pinetime::Controllers::CompanionStatusFlag::StoreInvalid) != 0,
          "decoder corruption fails closed and disables persistence writes");

    ConnectAndDisconnect(adapter, Peer(2));
    Check(ReadBytes(path) == corrupt, "invalid live-file evidence is preserved while writes are disabled");

    adapter.ForgetAllBonds();
    adapter.AdvanceTime(Pinetime::Controllers::BondPersistenceCoordinator::CriticalSettleMs);
    Pinetime::Controllers::NimbleBondStoreSnapshot recovered;
    Check(adapter.Query().persistenceWritesEnabled && DecodeFile(path, recovered) && recovered.registry.count == 0,
          "explicit forget-all re-enables writes and atomically replaces invalid evidence with an empty snapshot");
    adapter.Reboot();
    Check(adapter.Query().persistence.bootState == Pinetime::Controllers::BondPersistenceCoordinator::BootState::Restored,
          "recovered empty snapshot restores normally");

    auto corruptAgain = ReadBytes(path);
    corruptAgain[Pinetime::Controllers::BondStoreCodec::HeaderSize] ^= 0x40;
    WriteBytes(path, corruptAgain);
    adapter.Reboot();
    Check(!adapter.Query().persistenceWritesEnabled, "second decoder corruption re-enters fail-closed mode");
    adapter.Reset(true);
    Pinetime::Controllers::NimbleBondStoreSnapshot resetSnapshot;
    Check(adapter.Query().persistenceWritesEnabled &&
            adapter.Query().persistence.bootState == Pinetime::Controllers::BondPersistenceCoordinator::BootState::InitializedEmpty &&
            DecodeFile(path, resetSnapshot),
          "explicit reset removes invalid evidence and commits a new empty migration snapshot");
    adapter.Reset(true);
  }

  {
    VirtualBleAdapter adapter("infinisim-ble-read-failure-test-store.bin");
    adapter.Reset(true);
    adapter.SetStoreFailure(VirtualBleAdapter::StoreFailure::Read);
    adapter.Reboot();
    Check(adapter.Query().persistence.bootState == Pinetime::Controllers::BondPersistenceCoordinator::BootState::RestoreFailed &&
            !adapter.Query().persistenceWritesEnabled,
          "injected persistence read failure disables writes");
    adapter.SetStoreFailure(VirtualBleAdapter::StoreFailure::None);
    adapter.Reset(true);
  }

  {
    VirtualBleAdapter adapter("infinisim-ble-forget-test-store.bin");
    adapter.Reset(true);
    ConnectAndDisconnect(adapter, Peer(1));
    ConnectAndDisconnect(adapter, Peer(2));
    const uint32_t resetEpoch = adapter.CompanionStatus().resetEpoch;
    adapter.ForgetAllBonds();
    adapter.AdvanceTime(Pinetime::Controllers::BondPersistenceCoordinator::CriticalSettleMs);
    Check(adapter.CompanionStatus().bondedCount == 0 && adapter.CompanionStatus().resetEpoch == resetEpoch + 1,
          "forget-all clears bonds and advances the real registry reset epoch");
    adapter.Reboot();
    Check(adapter.Query().retainedPeers == 0 &&
            adapter.Query().persistence.bootState == Pinetime::Controllers::BondPersistenceCoordinator::BootState::Restored,
          "empty forget-all snapshot restores across reboot");
    adapter.Reset(true);
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
