#pragma once

#include "components/ble/BleRadioStateMachine.h"
#include "components/ble/BondPersistenceCoordinator.h"
#include "components/ble/BondStorePolicy.h"
#include "components/ble/BondStoreSnapshot.h"
#include "components/ble/CompanionManagementStatus.h"

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace InfiniSim::Ble {
  enum class VirtualSecurity : uint8_t {
    Unauthenticated,
    Bonded,
    Authenticated,
  };

  struct VirtualPeer {
    Pinetime::Controllers::BondRegistry::PeerIdentity identity {1, {1, 2, 3, 4, 5, 6}};
    VirtualSecurity security = VirtualSecurity::Authenticated;
    bool replaceBond = false;
  };

  class VirtualBleAdapter {
  public:
    using Radio = Pinetime::Controllers::BleRadioStateMachine;
    using BondPersistence = Pinetime::Controllers::BondPersistenceCoordinator;
    using BondPolicy = Pinetime::Controllers::BondStorePolicy;
    using BondSnapshot = Pinetime::Controllers::NimbleBondStoreSnapshot;

    enum class AttachResult : uint8_t {
      Attached,
      Busy,
      Rejected,
    };

    enum class GapOperation : uint8_t {
      Start,
      Stop,
      Terminate,
    };

    struct InjectedGapResult {
      int code = 0;
      Radio::Result classification = Radio::Result::Success;
    };

    enum class StoreFailure : uint8_t {
      None,
      Read,
      Write,
    };

    enum class PowerCut : uint8_t {
      None,
      BeforeReplace,
      AfterPartialStagedWrite,
      AfterStagedWrite,
    };

    struct Counters {
      uint32_t gapStarts = 0;
      uint32_t gapStops = 0;
      uint32_t gapTerminates = 0;
      uint32_t hostPolicyEvents = 0;
      uint32_t persistenceWriteAttempts = 0;
      uint64_t persistenceWakeLockDurationMs = 0;
    };

    struct State {
      Radio::DesiredMode desired = Radio::DesiredMode::Connectable;
      Radio::Mode actual = Radio::Mode::Off;
      bool virtualAdvertisingCommandActive = false;
      bool connected = false;
      bool bonded = false;
      bool authenticated = false;
      bool persistenceWritesEnabled = true;
      uint32_t nowMs = 0;
      uint8_t retainedPeers = 0;
      uint8_t cccdCount = 0;
      uint32_t evictions = 0;
      uint32_t radioRecoveries = 0;
      int lastStartResult = 0;
      int lastStopResult = 0;
      int lastTerminateResult = 0;
      uint8_t retryCount = 0;
      StoreFailure storeFailure = StoreFailure::None;
      PowerCut powerCut = PowerCut::None;
      Counters counters {};
      BondPersistence::Diagnostics persistence {};
    };

    explicit VirtualBleAdapter(std::string persistencePath = "infinisim-ble-bonds.bin");

    void Initialize();
    void Reboot();
    void Reset(bool removePersistentFile);

    void SetDesiredMode(Radio::DesiredMode mode);
    void RequestFastConnectable();
    void InjectGapResult(GapOperation operation, InjectedGapResult result);
    void AdvanceTime(uint32_t milliseconds);
    void Drain();

    void SetNextPeer(const VirtualPeer& peer);
    const VirtualPeer& NextPeer() const;
    AttachResult ConnectNextPeer();
    void Disconnect();
    bool Connected() const;
    bool Authenticated() const;
    bool Bonded() const;
    const std::optional<VirtualPeer>& ActivePeer() const;
    bool RetainsPeer(const Pinetime::Controllers::BondRegistry::PeerIdentity& peer) const;
    const Pinetime::Controllers::BondRegistry::Entry& RetainedPeer(size_t index) const;
    bool ConsumeTerminationRequest();
    void ForgetAllBonds();

    bool WriteActivePeerCccd(uint16_t handle, uint16_t flags);

    void SetStoreFailure(StoreFailure failure);
    void SetPowerCut(PowerCut cut);

    State Query() const;
    Pinetime::Controllers::CompanionManagementStatus CompanionStatus() const;

    const BondPersistence::Diagnostics& PersistenceDiagnostics() const;

    static const char* ToString(VirtualSecurity security);
    static const char* ToString(StoreFailure failure);
    static const char* ToString(PowerCut cut);

  private:
    static constexpr uint32_t VirtualWriteDurationMs = 2;

    InjectedGapResult NextGapResult(GapOperation operation);
    void ExecuteRadioCommand(Radio::Command command);
    void UpdateRadioTimers();
    void ObserveBondDirty();
    bool BondActivePeer();
    bool AddSecurityRecord(bool ours, const VirtualPeer& peer);
    void RemovePeerRecords(const Pinetime::Controllers::BondRegistry::PeerIdentity& peer);
    static Pinetime::Controllers::BondSecurityRecord SecurityFixture(const VirtualPeer& peer, bool ours);
    BondSnapshot CaptureSnapshot() const;
    bool RestoreSnapshot(const BondSnapshot& snapshot);
    bool InitializeEmptyPersistence(const BondSnapshot& previous, bool legacyReset);
    void PollPersistence();
    bool ReadPersistenceFile();
    bool WritePersistenceFile(const uint8_t* data, size_t size);
    void RemovePersistenceFiles();

    std::string persistencePath;
    std::string stagedPersistencePath;
    Radio radio;
    BondPolicy bondPolicy;
    BondPersistence bondPersistence;
    BondSnapshot store;
    VirtualPeer nextPeer;
    std::optional<VirtualPeer> activePeer;
    std::deque<InjectedGapResult> startResults;
    std::deque<InjectedGapResult> stopResults;
    std::deque<InjectedGapResult> terminateResults;
    Counters counters;
    StoreFailure storeFailure = StoreFailure::None;
    PowerCut powerCut = PowerCut::None;
    uint32_t nowMs = 0;
    uint32_t fastTimeoutAtMs = 0;
    uint32_t retryAtMs = 0;
    bool fastTimeoutScheduled = false;
    bool retryScheduled = false;
    bool initialized = false;
    bool persistenceWritesEnabled = true;
    bool formatInitializationPending = false;
    bool formatInitializationLegacyReset = false;
    uint64_t formatInitializationGeneration = 0;
    Radio::DesiredMode requestedMode = Radio::DesiredMode::Connectable;
    bool virtualAdvertisingCommandActive = false;
    bool terminationRequested = false;
  };
}
