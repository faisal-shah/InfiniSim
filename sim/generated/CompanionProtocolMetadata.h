// Generated from protocol/companion.json.
// Manifest SHA-256: 1cdc7fce2dc132ec67cdd7d34c4fd724290d4ab637c10b742b37f13f5a6eacc2
// Do not edit by hand.
#pragma once

#include "generated/CompanionProtocol.h"

#include <array>
#include <cstdint>

namespace SimCompanionProtocol {
  enum Access : uint8_t {
    Read = 1u << 0,
    Write = 1u << 1,
    WriteWithoutResponse = 1u << 2,
    Notify = 1u << 3,
    Indicate = 1u << 4,
  };

  struct CharacteristicMetadata {
    BridgeChar id;
    const char* name;
    uint8_t access;
    bool authenticated;
  };

  inline constexpr std::array<CharacteristicMetadata, 36> Characteristics {{
    {BridgeChar::ScheduleSync, "schedule_sync", Write, true},
    {BridgeChar::ScheduleDigest, "schedule_digest", Read, true},
    {BridgeChar::CurrentTime, "current_time", Write, false},
    {BridgeChar::NewAlert, "new_alert", Write, false},
    {BridgeChar::Battery, "battery", Read | Notify, false},
    {BridgeChar::EventRead, "event_read", Read | Write, true},
    {BridgeChar::PrayerSettings, "prayer_settings", Read | Write, true},
    {BridgeChar::BeaconKey, "beacon_key", Read | Write, true},
    {BridgeChar::BeaconControl, "beacon_control", Write, true},
    {BridgeChar::MultiAlarm, "multi_alarm", Read | Write, true},
    {BridgeChar::DfuControl, "dfu_control", Write | Notify, false},
    {BridgeChar::DfuPacket, "dfu_packet", WriteWithoutResponse, false},
    {BridgeChar::FsTransfer, "fs_transfer", Read | Write | Notify, false},
    {BridgeChar::FirmwareRevision, "firmware_revision", Read, false},
    {BridgeChar::Weather, "weather", Write, false},
    {BridgeChar::Steps, "steps", Read | Notify, false},
    {BridgeChar::StepsYesterday, "steps_yesterday", Read, false},
    {BridgeChar::MusicStatus, "music_status", Read | Write, false},
    {BridgeChar::MusicArtist, "music_artist", Read | Write, false},
    {BridgeChar::MusicTrack, "music_track", Read | Write, false},
    {BridgeChar::MusicAlbum, "music_album", Read | Write, false},
    {BridgeChar::MusicPosition, "music_position", Read | Write, false},
    {BridgeChar::MusicTotalLength, "music_total_length", Read | Write, false},
    {BridgeChar::MusicTrackNumber, "music_track_number", Read | Write, false},
    {BridgeChar::MusicTrackTotal, "music_track_total", Read | Write, false},
    {BridgeChar::MusicPlaybackSpeed, "music_playback_speed", Read | Write, false},
    {BridgeChar::MusicRepeat, "music_repeat", Read | Write, false},
    {BridgeChar::MusicShuffle, "music_shuffle", Read | Write, false},
    {BridgeChar::MusicEvent, "music_event", Notify, false},
    {BridgeChar::CallEvent, "call_event", Notify, false},
    {BridgeChar::TasksSync, "tasks_sync", Write, true},
    {BridgeChar::TasksDigest, "tasks_digest", Read, true},
    {BridgeChar::TaskRead, "task_read", Read | Write, true},
    {BridgeChar::CompanionStatus, "companion_status", Read, false},
    {BridgeChar::CompanionVerify, "companion_verify", Read, true},
    {BridgeChar::FamilyStateStatus, "family_state_status", Read, false},
  }};

  constexpr const CharacteristicMetadata* Metadata(uint8_t id) {
    if (id >= Characteristics.size()) {
      return nullptr;
    }
    const auto& metadata = Characteristics[id];
    return static_cast<uint8_t>(metadata.id) == id ? &metadata : nullptr;
  }
}
