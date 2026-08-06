#include "ble/VirtualBleAdapter.h"

#include "components/ble/BondStoreCodec.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

using Pinetime::Controllers::BondRegistry;
using Pinetime::Controllers::BondSecurityRecord;
using Pinetime::Controllers::BondStoreCodec;

namespace {
  bool Reached(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
  }

  template <typename Records, typename Predicate>
  void EraseRecords(Records& records, uint8_t& count, Predicate predicate) {
    uint8_t write = 0;
    for (uint8_t read = 0; read < count; read++) {
      if (!predicate(records[read])) {
        records[write++] = records[read];
      }
    }
    for (uint8_t index = write; index < count; index++) {
      records[index] = {};
    }
    count = write;
  }
}

namespace InfiniSim::Ble {
  VirtualBleAdapter::VirtualBleAdapter(std::string persistencePath)
    : persistencePath {std::move(persistencePath)}, stagedPersistencePath {this->persistencePath + ".next"} {
  }

  void VirtualBleAdapter::Initialize() {
    if (initialized) {
      return;
    }
    initialized = true;
    radio.SetIdentityAddressIsRandom(true);
    radio.OnHostSync();
    bootPersistenceGate.BeginRestore();
    radio.SetDesiredMode(Radio::DesiredMode::Off);
    counters.hostPolicyEvents++;
    ReadPersistenceFile();
    Drain();
  }

  void VirtualBleAdapter::Reboot() {
    radio = {};
    bondPolicy = {};
    bondPersistence = {};
    store = {};
    activePeer.reset();
    counters = {};
    nowMs = 0;
    fastTimeoutAtMs = 0;
    retryAtMs = 0;
    fastTimeoutScheduled = false;
    retryScheduled = false;
    persistenceWritesEnabled = true;
    bootPersistenceGate = {};
    requestedMode = Radio::DesiredMode::Connectable;
    virtualAdvertisingCommandActive = false;
    terminationRequested = false;
    initialized = false;
    Initialize();
  }

  void VirtualBleAdapter::Reset(bool removePersistentFile) {
    if (removePersistentFile) {
      RemovePersistenceFiles();
    }
    nextPeer = {};
    startResults.clear();
    stopResults.clear();
    terminateResults.clear();
    storeFailure = StoreFailure::None;
    powerCut = PowerCut::None;
    Reboot();
  }

  void VirtualBleAdapter::SetDesiredMode(Radio::DesiredMode mode) {
    Initialize();
    requestedMode = mode;
    radio.SetDesiredMode(bootPersistenceGate.BlocksRadio() ? Radio::DesiredMode::Off : requestedMode);
    counters.hostPolicyEvents++;
    Drain();
  }

  void VirtualBleAdapter::RequestFastConnectable() {
    Initialize();
    radio.RequestFastConnectable();
    counters.hostPolicyEvents++;
    Drain();
  }

  void VirtualBleAdapter::InjectGapResult(GapOperation operation, InjectedGapResult result) {
    switch (operation) {
      case GapOperation::Start:
        startResults.push_back(result);
        break;
      case GapOperation::Stop:
        stopResults.push_back(result);
        break;
      case GapOperation::Terminate:
        terminateResults.push_back(result);
        break;
    }
  }

  void VirtualBleAdapter::AdvanceTime(uint32_t milliseconds) {
    Initialize();
    nowMs += milliseconds;
    Drain();
  }

  void VirtualBleAdapter::SetNextPeer(const VirtualPeer& peer) {
    nextPeer = peer;
  }

  const VirtualPeer& VirtualBleAdapter::NextPeer() const {
    return nextPeer;
  }

  VirtualBleAdapter::AttachResult VirtualBleAdapter::ConnectNextPeer() {
    Initialize();
    if (bootPersistenceGate.BlocksRadio()) {
      return AttachResult::Rejected;
    }
    if (activePeer.has_value()) {
      return AttachResult::Busy;
    }
    if (!BondRegistry::IsValidPeer(nextPeer.identity)) {
      return AttachResult::Rejected;
    }

    activePeer = nextPeer;
    if (nextPeer.security != VirtualSecurity::Unauthenticated && !BondActivePeer()) {
      activePeer.reset();
      return AttachResult::Rejected;
    }
    if (nextPeer.security != VirtualSecurity::Unauthenticated && !nextPeer.replaceBond) {
      bondPolicy.OnKnownConnection(nextPeer.identity);
    }
    radio.OnConnected();
    virtualAdvertisingCommandActive = false;
    fastTimeoutScheduled = false;
    retryScheduled = false;
    counters.hostPolicyEvents++;
    ObserveBondDirty();
    return AttachResult::Attached;
  }

  void VirtualBleAdapter::Disconnect() {
    if (!activePeer.has_value()) {
      return;
    }
    activePeer.reset();
    radio.OnDisconnected();
    bondPersistence.OnDisconnect(bondPolicy.Dirty(), nowMs);
    counters.hostPolicyEvents++;
    terminationRequested = false;
    Drain();
  }

  bool VirtualBleAdapter::Connected() const {
    return activePeer.has_value();
  }

  bool VirtualBleAdapter::Authenticated() const {
    return activePeer.has_value() && activePeer->security == VirtualSecurity::Authenticated;
  }

  bool VirtualBleAdapter::Bonded() const {
    return activePeer.has_value() && activePeer->security != VirtualSecurity::Unauthenticated;
  }

  const std::optional<VirtualPeer>& VirtualBleAdapter::ActivePeer() const {
    return activePeer;
  }

  bool VirtualBleAdapter::RetainsPeer(const BondRegistry::PeerIdentity& peer) const {
    return bondPolicy.Registry().Contains(peer);
  }

  const BondRegistry::Entry& VirtualBleAdapter::RetainedPeer(size_t index) const {
    return bondPolicy.Registry().At(index);
  }

  bool VirtualBleAdapter::ConsumeTerminationRequest() {
    const bool requested = terminationRequested;
    terminationRequested = false;
    return requested;
  }

  void VirtualBleAdapter::ForgetAllBonds() {
    store = {};
    bondPolicy.OnForgetAll();
    persistenceWritesEnabled = true;
    counters.hostPolicyEvents++;
    ObserveBondDirty();
  }

  bool VirtualBleAdapter::WriteActivePeerCccd(uint16_t handle, uint16_t flags) {
    if (!activePeer.has_value() || !Bonded() || handle == 0 || (flags & 0x0003u) == 0 || (flags & ~0x0083u) != 0) {
      return false;
    }
    for (uint8_t index = 0; index < store.cccdCount; index++) {
      auto& record = store.cccds[index];
      if (record.peer == activePeer->identity && record.handle == handle) {
        record.flags = flags;
        record.valueChanged = true;
        bondPolicy.OnRecordWritten(BondPolicy::ObjectType::Cccd);
        counters.hostPolicyEvents++;
        ObserveBondDirty();
        return true;
      }
    }
    if (store.cccdCount >= store.cccds.size()) {
      bondPolicy.PlanOverflow(BondPolicy::ObjectType::Cccd, activePeer->identity);
      return false;
    }
    store.cccds[store.cccdCount++] = {activePeer->identity, handle, flags, true};
    bondPolicy.OnRecordWritten(BondPolicy::ObjectType::Cccd);
    counters.hostPolicyEvents++;
    ObserveBondDirty();
    return true;
  }

  void VirtualBleAdapter::SetStoreFailure(StoreFailure failure) {
    storeFailure = failure;
  }

  void VirtualBleAdapter::SetPowerCut(PowerCut cut) {
    powerCut = cut;
  }

  VirtualBleAdapter::State VirtualBleAdapter::Query() const {
    return {
      radio.Desired(),
      radio.Actual(),
      virtualAdvertisingCommandActive,
      Connected(),
      Bonded(),
      Authenticated(),
      persistenceWritesEnabled,
      nowMs,
      store.registry.count,
      store.cccdCount,
      bondPolicy.EvictionCount(),
      radio.RecoveryCount(),
      radio.LastStartResult(),
      radio.LastStopResult(),
      radio.LastTerminateResult(),
      radio.RetryCount(),
      storeFailure,
      powerCut,
      counters,
      bondPersistence.GetDiagnostics(),
    };
  }

  Pinetime::Controllers::CompanionManagementStatus VirtualBleAdapter::CompanionStatus() const {
    using namespace Pinetime::Controllers;
    CompanionManagementStatus status;
    status.bondedCount = static_cast<uint8_t>(bondPolicy.Registry().Count());
    status.resetEpoch = bondPolicy.Registry().ResetEpoch();
    status.evictionCount = bondPolicy.EvictionCount();
    status.cccdOverflowRejections = bondPolicy.CccdOverflowRejections();
    status.invariantViolations = bondPolicy.InvariantViolations();
    const auto& diagnostics = bondPersistence.GetDiagnostics();
    if (diagnostics.legacyResetThisBoot) {
      status.flags |= CompanionStatusFlag::LegacyResetThisBoot;
    }
    if (!persistenceWritesEnabled) {
      status.flags |= CompanionStatusFlag::StoreInvalid;
    }
    if (diagnostics.pending || diagnostics.inFlight) {
      status.flags |= CompanionStatusFlag::WritePendingOrInFlight;
    }
    if (diagnostics.criticalDirty) {
      status.flags |= CompanionStatusFlag::CriticalDirty;
    }
    if (diagnostics.usageDirty) {
      status.flags |= CompanionStatusFlag::UsageDirty;
    }
    if (bootPersistenceGate.FormatPending()) {
      status.flags |= CompanionStatusFlag::FormatInitializationPending;
    }
    return status;
  }

  const VirtualBleAdapter::BondPersistence::Diagnostics& VirtualBleAdapter::PersistenceDiagnostics() const {
    return bondPersistence.GetDiagnostics();
  }

  const char* VirtualBleAdapter::ToString(VirtualSecurity security) {
    switch (security) {
      case VirtualSecurity::Unauthenticated:
        return "unauthenticated";
      case VirtualSecurity::Bonded:
        return "bonded";
      case VirtualSecurity::Authenticated:
        return "authenticated";
    }
    return "?";
  }

  const char* VirtualBleAdapter::ToString(StoreFailure failure) {
    switch (failure) {
      case StoreFailure::None:
        return "none";
      case StoreFailure::Read:
        return "read";
      case StoreFailure::Write:
        return "write";
    }
    return "?";
  }

  const char* VirtualBleAdapter::ToString(PowerCut cut) {
    switch (cut) {
      case PowerCut::None:
        return "none";
      case PowerCut::BeforeReplace:
        return "before_replace";
      case PowerCut::AfterPartialStagedWrite:
        return "after_partial_staged_write";
      case PowerCut::AfterStagedWrite:
        return "after_staged_write";
    }
    return "?";
  }

  VirtualBleAdapter::InjectedGapResult VirtualBleAdapter::NextGapResult(GapOperation operation) {
    auto* queue = &startResults;
    if (operation == GapOperation::Stop) {
      queue = &stopResults;
    } else if (operation == GapOperation::Terminate) {
      queue = &terminateResults;
    }
    if (queue->empty()) {
      return {};
    }
    const auto result = queue->front();
    queue->pop_front();
    return result;
  }

  void VirtualBleAdapter::ExecuteRadioCommand(Radio::Command command) {
    GapOperation operation = GapOperation::Start;
    switch (command) {
      case Radio::Command::StartFastAdvertising:
      case Radio::Command::StartSlowAdvertising:
      case Radio::Command::StartBeaconAdvertising:
        counters.gapStarts++;
        operation = GapOperation::Start;
        break;
      case Radio::Command::StopAdvertising:
        counters.gapStops++;
        operation = GapOperation::Stop;
        break;
      case Radio::Command::TerminateConnection:
        counters.gapTerminates++;
        operation = GapOperation::Terminate;
        break;
      case Radio::Command::RestoreIdentityAddress:
        operation = GapOperation::Stop;
        break;
      case Radio::Command::None:
        return;
    }

    const auto result = NextGapResult(operation);
    radio.Complete(command, result.code, result.classification);
    counters.hostPolicyEvents++;
    if (result.classification == Radio::Result::Success || result.classification == Radio::Result::AlreadyInactive) {
      if (operation == GapOperation::Start) {
        virtualAdvertisingCommandActive = true;
        if (command == Radio::Command::StartFastAdvertising) {
          fastTimeoutAtMs = nowMs + Radio::FastDurationMs;
          fastTimeoutScheduled = true;
        } else {
          fastTimeoutScheduled = false;
        }
      } else if (operation == GapOperation::Stop) {
        virtualAdvertisingCommandActive = false;
        fastTimeoutScheduled = false;
      } else if (operation == GapOperation::Terminate) {
        terminationRequested = true;
      }
    }
    if (radio.RetryWaiting()) {
      retryAtMs = nowMs + radio.RetryDelayMs();
      retryScheduled = true;
    }
  }

  void VirtualBleAdapter::UpdateRadioTimers() {
    if (retryScheduled && Reached(nowMs, retryAtMs)) {
      retryScheduled = false;
      radio.OnRetryTimeout();
      counters.hostPolicyEvents++;
    }
    if (fastTimeoutScheduled && Reached(nowMs, fastTimeoutAtMs)) {
      fastTimeoutScheduled = false;
      radio.OnFastTimeout();
      counters.hostPolicyEvents++;
    }
  }

  void VirtualBleAdapter::Drain() {
    if (!initialized) {
      return;
    }
    for (uint8_t iteration = 0; iteration < 64; iteration++) {
      UpdateRadioTimers();
      const auto action = radio.Step();
      if (action.command != Radio::Command::None) {
        ExecuteRadioCommand(action.command);
        continue;
      }
      if (!persistenceWritesEnabled) {
        break;
      }
      const auto before = bondPersistence.Poll(nowMs);
      PollPersistence();
      if (before == BondPersistence::Action::None) {
        break;
      }
    }
  }

  void VirtualBleAdapter::ObserveBondDirty() {
    store.registry = bondPolicy.CaptureRegistry();
    store.generation = bondPolicy.Generation();
    bondPersistence.ObserveDirty(bondPolicy.Dirty(), nowMs, Connected());
    PollPersistence();
  }

  bool VirtualBleAdapter::BondActivePeer() {
    if (!activePeer.has_value()) {
      return false;
    }
    const auto peer = activePeer->identity;
    const bool exists = bondPolicy.Registry().Contains(peer);
    if (exists && activePeer->replaceBond) {
      RemovePeerRecords(peer);
      if (!bondPolicy.OnPeerForgotten(peer)) {
        return false;
      }
    } else if (exists) {
      return true;
    }

    if (!AddSecurityRecord(true, *activePeer) || !AddSecurityRecord(false, *activePeer)) {
      return false;
    }
    bondPolicy.OnBondEstablished(peer);
    counters.hostPolicyEvents++;
    ObserveBondDirty();
    return true;
  }

  bool VirtualBleAdapter::AddSecurityRecord(bool ours, const VirtualPeer& peer) {
    auto& records = ours ? store.ourSecs : store.peerSecs;
    auto& count = ours ? store.ourSecCount : store.peerSecCount;
    const auto type = ours ? BondPolicy::ObjectType::OurSecurity : BondPolicy::ObjectType::PeerSecurity;
    for (uint8_t index = 0; index < count; index++) {
      if (records[index].peer == peer.identity) {
        records[index] = SecurityFixture(peer, ours);
        bondPolicy.OnRecordWritten(type);
        return true;
      }
    }

    if (count >= records.size()) {
      const auto plan = bondPolicy.PlanOverflow(type, peer.identity);
      if (plan.action != BondPolicy::OverflowAction::Evict) {
        return false;
      }
      RemovePeerRecords(plan.evict);
      if (!bondPolicy.CommitEviction(plan.evict)) {
        return false;
      }
    }
    records[count++] = SecurityFixture(peer, ours);
    bondPolicy.OnRecordWritten(type);
    return true;
  }

  void VirtualBleAdapter::RemovePeerRecords(const BondRegistry::PeerIdentity& peer) {
    const uint8_t ourBefore = store.ourSecCount;
    const uint8_t peerBefore = store.peerSecCount;
    const uint8_t cccdBefore = store.cccdCount;
    EraseRecords(store.ourSecs, store.ourSecCount, [&peer](const auto& record) {
      return record.peer == peer;
    });
    EraseRecords(store.peerSecs, store.peerSecCount, [&peer](const auto& record) {
      return record.peer == peer;
    });
    EraseRecords(store.cccds, store.cccdCount, [&peer](const auto& record) {
      return record.peer == peer;
    });
    if (store.ourSecCount != ourBefore) {
      bondPolicy.OnRecordDeleted(BondPolicy::ObjectType::OurSecurity);
    }
    if (store.peerSecCount != peerBefore) {
      bondPolicy.OnRecordDeleted(BondPolicy::ObjectType::PeerSecurity);
    }
    if (store.cccdCount != cccdBefore) {
      bondPolicy.OnRecordDeleted(BondPolicy::ObjectType::Cccd);
    }
  }

  BondSecurityRecord VirtualBleAdapter::SecurityFixture(const VirtualPeer& peer, bool ours) {
    BondSecurityRecord record;
    record.peer = peer.identity;
    record.keySize = 16;
    record.ediv = static_cast<uint16_t>(0x5100u | peer.identity.address[0]);
    record.rand = 0x4649585455524500ull | peer.identity.address[0];
    for (size_t index = 0; index < record.ltk.size(); index++) {
      const uint8_t seed = static_cast<uint8_t>(peer.identity.address[index % peer.identity.address.size()] + index + (ours ? 0x10 : 0x40));
      record.ltk[index] = seed;
      record.irk[index] = static_cast<uint8_t>(seed ^ 0x5a);
      record.csrk[index] = static_cast<uint8_t>(seed ^ 0xa5);
    }
    record.ltkPresent = true;
    record.irkPresent = true;
    record.csrkPresent = true;
    record.authenticated = peer.security == VirtualSecurity::Authenticated;
    record.secureConnections = true;
    return record;
  }

  VirtualBleAdapter::BondSnapshot VirtualBleAdapter::CaptureSnapshot() const {
    BondSnapshot snapshot = store;
    snapshot.registry = bondPolicy.CaptureRegistry();
    snapshot.generation = bondPolicy.Generation();
    return snapshot;
  }

  bool VirtualBleAdapter::RestoreSnapshot(const BondSnapshot& snapshot) {
    if (!BondStoreCodec::Validate(snapshot) || !bondPolicy.RestoreRegistry(snapshot.registry, snapshot.generation)) {
      return false;
    }
    store = snapshot;
    return true;
  }

  bool VirtualBleAdapter::InitializeEmptyPersistence(const BondSnapshot& previous, bool legacyReset) {
    BondSnapshot empty;
    empty.registry.resetEpoch = previous.registry.resetEpoch + 1;
    empty.generation = previous.generation + 1;
    if (!bondPersistence.Capture(empty)) {
      persistenceWritesEnabled = false;
      bootPersistenceGate.CompleteRestore(false);
      bondPersistence.RecordBoot(BondPersistence::BootState::RestoreFailed);
      return false;
    }
    if (!RestoreSnapshot(empty)) {
      persistenceWritesEnabled = false;
      bootPersistenceGate.CompleteRestore(false);
      bondPersistence.RecordBoot(BondPersistence::BootState::RestoreFailed);
      return false;
    }

    persistenceWritesEnabled = true;
    bootPersistenceGate.BeginFormatInitialization(empty.generation, legacyReset);
    bootPersistenceGate.CompleteRestore(true);
    radio.SetDesiredMode(Radio::DesiredMode::Off);
    bondPersistence.RecordBoot(BondPersistence::BootState::InitializingEmpty);
    return true;
  }

  bool VirtualBleAdapter::RestoreEmptyFailClosed(BondPersistence::BootState state,
                                                 BondStoreCodec::DecodeError error) {
    BondSnapshot empty;
    const bool restored = RestoreSnapshot(empty);
    persistenceWritesEnabled = false;
    bootPersistenceGate.CompleteRestore(restored);
    bondPersistence.RecordBoot(restored ? state : BondPersistence::BootState::RestoreFailed,
                               restored ? error : BondStoreCodec::DecodeError::None);
    if (restored) {
      radio.SetDesiredMode(requestedMode);
    }
    return false;
  }

  void VirtualBleAdapter::PollPersistence() {
    if (!persistenceWritesEnabled) {
      return;
    }
    switch (bondPersistence.Poll(nowMs)) {
      case BondPersistence::Action::None:
        return;
      case BondPersistence::Action::Capture:
        if (!bondPersistence.Capture(CaptureSnapshot())) {
          bondPersistence.CaptureUnstable(nowMs);
        }
        return;
      case BondPersistence::Action::QueueWrite: {
        bondPersistence.MarkWriteQueued();
        const auto write = bondPersistence.CurrentWrite();
        counters.persistenceWriteAttempts++;
        counters.persistenceWakeLockDurationMs += VirtualWriteDurationMs;
        const bool success = write && WritePersistenceFile(write.data, write.size);
        if (success) {
          bondPolicy.AcknowledgePersisted(write.generation);
        }
        bondPersistence.WriteCompleted(success,
                                       VirtualWriteDurationMs,
                                       success ? static_cast<uint32_t>(write.size) : 0,
                                       bondPolicy.Dirty(),
                                       nowMs,
                                       Connected());
        if (bootPersistenceGate.CompleteFormatWrite(success, write.generation)) {
          bondPersistence.RecordBoot(BondPersistence::BootState::InitializedEmpty,
                                     BondStoreCodec::DecodeError::None,
                                     bootPersistenceGate.FormatNoticePending());
          if (!bootPersistenceGate.BlocksRadio()) {
            radio.SetDesiredMode(requestedMode);
          }
        }
        store.registry = bondPolicy.CaptureRegistry();
        store.generation = bondPolicy.Generation();
        return;
      }
    }
  }

  bool VirtualBleAdapter::ReadPersistenceFile() {
    if (storeFailure == StoreFailure::Read) {
      persistenceWritesEnabled = false;
      bootPersistenceGate.CompleteRestore(false);
      bondPersistence.RecordBoot(BondPersistence::BootState::RestoreFailed);
      return false;
    }

    std::error_code existsError;
    const bool exists = std::filesystem::exists(persistencePath, existsError);
    if (existsError) {
      persistenceWritesEnabled = false;
      bootPersistenceGate.CompleteRestore(false);
      bondPersistence.RecordBoot(BondPersistence::BootState::RestoreFailed);
      return false;
    }
    if (!exists) {
      return InitializeEmptyPersistence({}, false);
    }

    std::ifstream input(persistencePath, std::ios::binary | std::ios::ate);
    if (!input) {
      return RestoreEmptyFailClosed(BondPersistence::BootState::Invalid,
                                    BondStoreCodec::DecodeError::Length);
    }
    const auto end = input.tellg();
    if (end < 0 || static_cast<uint64_t>(end) > BondStoreCodec::MaxEncodedSize) {
      return RestoreEmptyFailClosed(BondPersistence::BootState::Invalid,
                                    BondStoreCodec::DecodeError::Length);
    }
    auto& buffer = bondPersistence.BootBuffer();
    const size_t size = static_cast<size_t>(end);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
    if (!input) {
      return RestoreEmptyFailClosed(BondPersistence::BootState::Invalid,
                                    BondStoreCodec::DecodeError::Length);
    }
    BondSnapshot snapshot;
    const auto decoded = BondStoreCodec::Decode(buffer.data(), size, snapshot);
    if (!decoded) {
      return RestoreEmptyFailClosed(BondPersistence::BootState::Invalid,
                                    decoded.error);
    }
    if (!decoded.formatInitialized) {
      return InitializeEmptyPersistence(snapshot, true);
    }
    if (!RestoreSnapshot(snapshot)) {
      persistenceWritesEnabled = false;
      bootPersistenceGate.CompleteRestore(false);
      bondPersistence.RecordBoot(BondPersistence::BootState::RestoreFailed);
      return false;
    }
    persistenceWritesEnabled = true;
    bootPersistenceGate.CompleteRestore(true);
    bondPersistence.RecordBoot(BondPersistence::BootState::Restored);
    radio.SetDesiredMode(requestedMode);
    return true;
  }

  bool VirtualBleAdapter::WritePersistenceFile(const uint8_t* data, size_t size) {
    if (storeFailure == StoreFailure::Write || powerCut == PowerCut::BeforeReplace) {
      return false;
    }

    if (powerCut == PowerCut::AfterPartialStagedWrite) {
      std::ofstream partial(stagedPersistencePath, std::ios::binary | std::ios::trunc);
      const size_t partialSize = std::max<size_t>(1, size / 2);
      partial.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(partialSize));
      partial.close();
      return false;
    }

    {
      std::ofstream staged(stagedPersistencePath, std::ios::binary | std::ios::trunc);
      if (!staged) {
        return false;
      }
      staged.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
      staged.close();
      if (!staged) {
        return false;
      }
    }
    if (powerCut == PowerCut::AfterStagedWrite) {
      return false;
    }

    std::error_code error;
    std::filesystem::rename(stagedPersistencePath, persistencePath, error);
    return !error;
  }

  void VirtualBleAdapter::RemovePersistenceFiles() {
    std::error_code error;
    std::filesystem::remove(persistencePath, error);
    error.clear();
    std::filesystem::remove(stagedPersistencePath, error);
  }
}
