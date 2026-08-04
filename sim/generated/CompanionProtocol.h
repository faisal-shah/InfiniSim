// Generated from protocol/companion.json.
// Manifest SHA-256: 6a3dc57bba4cb3ef146be88209256026e5a531f0fbad6079e34a14692dcd0949
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
  };
}
