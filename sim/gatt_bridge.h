#pragma once

// TCP "GATT bridge": exposes the watch's BLE characteristics over a local TCP
// socket so a companion app (or test script) can drive the simulator with the
// exact bytes it would write over the radio. This is the no-hardware
// end-to-end link between the PineTimeCompanion Android app running in an
// emulator (host reachable as 10.0.2.2) and InfiniSim.
//
// Protocol (all little-endian). Ops: 0 = write, 1 = read, 2 = write-without-
// response (processed, but no response frame is sent).
//   request:       [charId u8][op u8][len u16][payload len bytes]
//   response:      [status u8: 0=ok, ATT error, 0xFE bad op, 0xFF unknown char]
//                  [len u16][payload len bytes]
//   notification:  [0xF0][charId u8][len u16][payload len bytes]  (unsolicited,
//                  watch -> client; interleaved between responses)
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
//   9     Multi-Alarm           00090001    write (CAS blob) / read (blob)
//  10     DFU Control Point     0x1531      write + notify (Nordic legacy DFU)
//  11     DFU Packet            0x1532      write-no-response (firmware chunks)
//  12     FS Transfer           adaf0200    write + notify (BLE filesystem)
//  13     Firmware Revision     0x2A26      read (version string)
//  14     Weather               00050001    write (current + forecast)
//  15     Step Count            00030001    read (u32 LE, today)
//  16     Step Count Yesterday  00030003    read (u32 LE, yesterday)
//  17-27  Music metadata        000000xx    write (status/artist/track/…)
//  28     Music Event           00000001    notify only (watch -> phone)
//  29     Call Event            00020001    notify only (watch -> phone)
//  30     Task Sync Command     000a0001    write -> TaskService::OnCommand
//  31     Task Digest           000a0002    read  -> TaskService::OnCommand
//  32     Task Read             000a0003    write (select index) / read (record)
//  33     Companion Status      000b0001    public read
//  34     Companion Verify      000b0002    authenticated read
//  35     Family State Status   000c0001    public read
//
// Single client at a time: additional clients receive a busy frame and are
// closed without disturbing the incumbent. The loopback-only BLE test-control
// endpoint is the only path that may explicitly force replacement.

#include <cstdint>
#include <cstddef>
#include <functional>
#include "generated/CompanionProtocol.h"
#include "ble/VirtualClientLifecycle.h"

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
  void ForceDisconnectClient();
  bool HasClient() const;

private:
  using CharId = SimCompanionProtocol::BridgeChar;

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
  void CloseClient(bool detachVirtualLink = true);
  bool AttachClient(int incoming);
  static void SendBusyAndClose(int incoming);

  Pinetime::System::SystemTask& systemTask;
  Pinetime::Controllers::DateTime& dateTimeController;
  Pinetime::Controllers::Battery& batteryController;
  Pinetime::Controllers::MotionController& motionController;
  Pinetime::Controllers::Ble& bleController;

  int listenFd = -1;
  int clientFd = -1;
  InfiniSim::Ble::VirtualClientLifecycle clientLifecycle;
  uint8_t rxBuffer[512];
  size_t rxLen = 0;
};
