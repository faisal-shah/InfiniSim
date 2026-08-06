// Generated from protocol/companion.json.
// Manifest SHA-256: f4881c3833b552227463a5af2f4a07f110ec1c67f7ff44d1f193af15b9d6750c
// Do not edit by hand.
#pragma once

#include <cstdint>

namespace SimCompanionProtocol {
  enum class BridgeChar : uint8_t {
    ScheduleSync = 0,
    ScheduleDigest = 1,
    CurrentTime = 2,
    NewAlert = 3,
    Battery = 4,
    EventRead = 5,
    PrayerSettings = 6,
    BeaconKey = 7,
    BeaconControl = 8,
    MultiAlarm = 9,
    DfuControl = 10,
    DfuPacket = 11,
    FsTransfer = 12,
    FirmwareRevision = 13,
    Weather = 14,
    Steps = 15,
    StepsYesterday = 16,
    MusicStatus = 17,
    MusicArtist = 18,
    MusicTrack = 19,
    MusicAlbum = 20,
    MusicPosition = 21,
    MusicTotalLength = 22,
    MusicTrackNumber = 23,
    MusicTrackTotal = 24,
    MusicPlaybackSpeed = 25,
    MusicRepeat = 26,
    MusicShuffle = 27,
    MusicEvent = 28,
    CallEvent = 29,
    TasksSync = 30,
    TasksDigest = 31,
    TaskRead = 32,
    CompanionStatus = 33,
    CompanionVerify = 34,
    FamilyStateStatus = 35,
  };
}
