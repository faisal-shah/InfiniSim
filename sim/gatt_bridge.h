#pragma once

// TCP "GATT bridge": exposes the watch's BLE characteristics over a local TCP
// socket so a companion app (or test script) can drive the simulator with the
// exact bytes it would write over the radio. This is the no-hardware
// end-to-end link between the PineTimeCompanion Android app running in an
// emulator (host reachable as 10.0.2.2) and InfiniSim.
//
// Protocol (all little-endian):
//   request:  [charId u8][op u8: 0=write, 1=read][len u16][payload len bytes]
//   response: [status u8: 0=ok][len u16][payload len bytes]
//
// charId  endpoint                          op
//   0     Schedule Sync Command 00060001    write -> ScheduleService::OnCommand
//   1     Schedule Digest       00060002    read  -> ScheduleService::OnCommand
//   2     Current Time          0x2A2B      write -> DateTimeController::SetTime
//   3     New Alert             0x2A46      write -> AlertNotificationService::OnAlert
//   4     Battery Level         0x2A19      read  -> BatteryController percent
//   5     Schedule Event Read   00060003    write (select index) / read (record)
//   6     Prayer Settings       00070001    write (9-byte blob) / read (blob)
//   7     Beacon Key            00080001    write (28-byte key) / read (hasKey)
//   8     Beacon Control        00080002    write (0x01 = enable)
//
// Single client at a time; polled from the SDL main loop (same thread as the
// keyboard injectors, so calling the GATT handlers directly is safe).

#include <cstdint>
#include <cstddef>
#include <functional>

struct ble_gatt_access_ctxt;

namespace Pinetime {
  namespace Controllers {
    class DateTime;
    class Battery;
    class MotionController;
    class Ble;
  }

  namespace System {
    class SystemTask;
  }
}

class GattBridge {
public:
  GattBridge(Pinetime::System::SystemTask& systemTask,
             Pinetime::Controllers::DateTime& dateTimeController,
             Pinetime::Controllers::Battery& batteryController,
             Pinetime::Controllers::MotionController& motionController,
             Pinetime::Controllers::Ble& bleController);
  ~GattBridge();

  bool Start(uint16_t port);
  void Poll(); // call every main-loop iteration; non-blocking

private:
  enum class CharId : uint8_t {
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
    DfuControl = 10, // 0x1531 write + notify (DFU control point)
    DfuPacket = 11, // 0x1532 write-without-response (DFU firmware/init/size data)
    FsTransfer = 12, // adaf0200 write + notify (BLE filesystem)
    FirmwareRevision = 13, // 0x2A26 read (firmware version string)
    Weather = 14, // 00050001 write (SimpleWeatherService: current + forecast)
    StepCount = 15, // 00030001 read (MotionService: today's cumulative steps)
    StepCountYesterday = 16, // 00030003 read (MotionService: yesterday's total)
    // MusicService writes (000000XX chars; firmware char byte = 0x02 + (id - 17)).
    MusicStatus = 17, // 00000002 write (1B playing)
    MusicArtist = 18, // 00000003 write (UTF-8)
    MusicTrack = 19, // 00000004 write (UTF-8)
    MusicAlbum = 20, // 00000005 write (UTF-8)
    MusicPosition = 21, // 00000006 write (u32 BE seconds)
    MusicTotalLength = 22, // 00000007 write (u32 BE seconds)
    MusicTrackNumber = 23, // 00000008 write (u32 BE)
    MusicTrackTotal = 24, // 00000009 write (u32 BE)
    MusicPlaybackSpeed = 25, // 0000000a write (u32 BE, speed x100)
    MusicRepeat = 26, // 0000000b write (1B)
    MusicShuffle = 27, // 0000000c write (1B)
    // Notify-only sources (watch -> phone), tagged by attribute handle.
    MusicEvent = 28, // 00000001 notify (1B event: open/play/pause/next/prev/vol)
    CallEvent = 29, // 00020001 notify (1B: 0=reject 1=accept 2=mute)
    TasksSync = 30, // 000a0001 write -> TaskService::OnCommand (Begin/record/Commit/Abort/SetStreak)
    TasksDigest = 31, // 000a0002 read  -> [protoVer][cap][count][taskVersion u32][streak u16]
    TaskRead = 32, // 000a0003 write (select index) / read (31-byte record)
  };

  // Response payloads are assembled into a fixed stack buffer of this size; the
  // capacity is handed to os_mbuf_append so an over-long service read is
  // rejected instead of overrunning it.
  static constexpr uint16_t kResponseBufferSize = 64;

  // How a characteristic is accessed over the bridge, and the op it requires:
  //   Write      op 0 only  -> forward as a GATT write
  //   Read       op 1 only  -> forward as a GATT read, return the payload
  //   WriteRead  op 0 write (e.g. select an index) / op 1 read (fetch a record)
  enum class Access { Write, Read, WriteRead };

  void HandleRequest();
  uint8_t Dispatch(uint8_t charId, uint8_t op, const uint8_t* payload, uint16_t len, uint8_t* out, uint16_t& outLen);
  // Build a fake GATT access to (charByte, serviceByte), invoke `call` (which
  // routes to the right firmware service), and for reads copy the produced
  // length into outLen. Returns the bridge status byte (0 ok, or an ATT error).
  uint8_t Forward(Access mode,
                  uint8_t op,
                  uint8_t charByte,
                  uint8_t serviceByte,
                  const std::function<int(ble_gatt_access_ctxt*)>& call,
                  const uint8_t* payload,
                  uint16_t len,
                  uint8_t* out,
                  uint16_t& outLen);
  void SendResponse(uint8_t status, const uint8_t* payload, uint16_t len);
  void SendNotification(uint8_t charId, const uint8_t* payload, uint16_t len);
  void DrainNotifications();
  void CloseClient();

  Pinetime::System::SystemTask& systemTask;
  Pinetime::Controllers::DateTime& dateTimeController;
  Pinetime::Controllers::Battery& batteryController;
  Pinetime::Controllers::MotionController& motionController;
  Pinetime::Controllers::Ble& bleController;

  int listenFd = -1;
  int clientFd = -1;
  uint8_t rxBuffer[512];
  size_t rxLen = 0;
};
