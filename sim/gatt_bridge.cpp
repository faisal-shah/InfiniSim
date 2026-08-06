#include "gatt_bridge.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "ble/VirtualGattAccessPolicy.h"
#include "components/ble/ScheduleService.h"
#include "components/ble/TaskService.h"
#include "components/ble/PrayerService.h"
#include "components/ble/BeaconService.h"
#include "components/ble/MultiAlarmService.h"
#include "components/ble/DfuService.h"
#include "components/ble/FSService.h"
#include "components/ble/AlertNotificationService.h"
#include "components/battery/BatteryController.h"
#include "components/ble/BleController.h"
#include "components/ble/MusicService.h"
#include "components/datetime/DateTimeController.h"
#include "components/motion/MotionController.h"
#include "systemtask/SystemTask.h"
#include "storagetask/StorageTask.h"
#include "notify_queue.h"
#include "Version.h"

namespace {
  constexpr size_t headerSize = 4; // charId u8, op u8, len u16

  // Build a fake single-buffer mbuf + access context around `data`.
  struct FakeGattAccess {
    os_mbuf buffer {};
    ble_gatt_chr_def chrDef {};
    ble_uuid128_t uuid {};
    ble_gatt_access_ctxt ctxt {};

    // capacity: bytes available at `data` for a read to append into (0 for
    // writes, which never append). os_mbuf_append bounds-checks against it.
    FakeGattAccess(uint8_t op, uint8_t charIdByte, uint8_t* data, uint16_t len, uint8_t serviceByte = 0x06, uint8_t capacity = 0) {
      uuid = ble_uuid128_t {
        .u = {.type = BLE_UUID_TYPE_128},
        .value = {0xd0, 0x42, 0x19, 0x3a, 0x3b, 0x43, 0x23, 0x8e, 0xfe, 0x48, 0xfc, 0x78, charIdByte, 0x00, serviceByte, 0x00}};
      chrDef.uuid = &uuid.u;
      buffer.om_data = data;
      buffer.om_len = op == BLE_GATT_ACCESS_OP_WRITE_CHR ? len : 0;
      buffer.om_pkthdr_len = capacity;
      ctxt.op = op;
      ctxt.om = &buffer;
      ctxt.chr = &chrDef;
    }
  };
}

GattBridge::GattBridge(Pinetime::System::SystemTask& systemTask,
                       Pinetime::Controllers::DateTime& dateTimeController,
                       Pinetime::Controllers::Battery& batteryController,
                       Pinetime::Controllers::MotionController& motionController,
                       Pinetime::Controllers::Ble& bleController)
  : systemTask {systemTask},
    dateTimeController {dateTimeController},
    batteryController {batteryController},
    motionController {motionController},
    bleController {bleController} {
}

GattBridge::~GattBridge() {
  CloseClient();
  if (listenFd >= 0) {
    close(listenFd);
  }
}

bool GattBridge::Start(uint16_t port) {
  // A companion that disconnects mid-response would otherwise raise SIGPIPE on
  // our write() and kill the whole simulator. Ignore it process-wide and rely
  // on write() returning EPIPE, which we turn into an orderly client close.
  signal(SIGPIPE, SIG_IGN);

  listenFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (listenFd < 0) {
    perror("gatt-bridge socket");
    return false;
  }
  const int one = 1;
  setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); // Android emulator reaches us via 10.0.2.2
  addr.sin_port = htons(port);
  if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(listenFd, 16) != 0) {
    perror("gatt-bridge bind/listen");
    close(listenFd);
    listenFd = -1;
    return false;
  }
  printf("InfiniSim: GATT bridge listening on port %u\n", port);
  return true;
}

void GattBridge::CloseClient(bool detachVirtualLink) {
  if (clientFd >= 0) {
    clientLifecycle.Detach(clientFd);
    close(clientFd);
    clientFd = -1;
    rxLen = 0;
    SimNotify::Clear(); // drop any pending notifications from the closed link
    if (detachVirtualLink) {
      systemTask.nimble().DetachVirtualLink();
    }
  }
}

bool GattBridge::AttachClient(int incoming) {
  if (clientLifecycle.TryAttach(incoming) == InfiniSim::Ble::VirtualClientLifecycle::AttachResult::Busy) {
    return false;
  }
  const auto attached = systemTask.nimble().AttachVirtualLink();
  if (attached != InfiniSim::Ble::VirtualBleAdapter::AttachResult::Attached) {
    clientLifecycle.Detach(incoming);
    return false;
  }
  clientFd = incoming;
  const int one = 1;
  setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  rxLen = 0;
  return true;
}

void GattBridge::SendBusyAndClose(int incoming) {
  static constexpr uint8_t BusyResponse[] {0xfd, 0x00, 0x00};
  send(incoming, BusyResponse, sizeof(BusyResponse), MSG_NOSIGNAL);
  close(incoming);
}

void GattBridge::ForceDisconnectClient() {
  CloseClient();
}

bool GattBridge::HasClient() const {
  return clientFd >= 0;
}

void GattBridge::Poll() {
  if (listenFd < 0) {
    return;
  }

  // Drain the accept queue, but preserve the incumbent. The firmware policy has
  // one active connection; a second transport client is explicitly busy.
  for (;;) {
    const int incoming = accept4(listenFd, nullptr, nullptr, SOCK_NONBLOCK);
    if (incoming < 0) {
      break;
    }
    if (!AttachClient(incoming)) {
      SendBusyAndClose(incoming);
    }
  }
  if (clientFd < 0) {
    return;
  }
  if (!systemTask.nimble().IsVirtualLinkConnected()) {
    CloseClient(false);
    return;
  }

  while (true) {
    const ssize_t n = read(clientFd, rxBuffer + rxLen, sizeof(rxBuffer) - rxLen);
    if (n > 0) {
      rxLen += n;
      HandleRequest();
      DrainNotifications();
      continue;
    }
    if (n == 0) {
      CloseClient(); // orderly shutdown
      return;
    }
    // EAGAIN: no request pending, but firmware may have queued async
    // notifications (DfuService AsyncSend fires on the SDL timer thread).
    DrainNotifications();
    return;
  }
}

void GattBridge::DrainNotifications() {
  if (clientFd < 0) {
    return;
  }
  uint16_t attHandle;
  uint8_t fallbackCharId;
  std::vector<uint8_t> bytes;
  while (SimNotify::Pop(attHandle, fallbackCharId, bytes)) {
    // Tag by the attribute handle the firmware notified on (see HandleForChr in
    // sim/host/ble_gatt.cpp); the legacy activeCharId is the fallback for any
    // source not mapped here.
    uint8_t charId;
    switch (attHandle) {
      case 0x0001: // MusicService event char (00000001)
        charId = static_cast<uint8_t>(CharId::MusicEvent);
        break;
      case 0x0201: // ANS call-event char (00020001)
        charId = static_cast<uint8_t>(CharId::CallEvent);
        break;
      case 0x1531: // DFU control point
      case 0x1534:
        charId = static_cast<uint8_t>(CharId::DfuControl);
        break;
      case 0x0200: // FS transfer char
        charId = static_cast<uint8_t>(CharId::FsTransfer);
        break;
      default:
        charId = fallbackCharId;
        break;
    }
    SendNotification(charId, bytes.data(), static_cast<uint16_t>(bytes.size()));
  }
}

void GattBridge::SendNotification(uint8_t charId, const uint8_t* payload, uint16_t len) {
  // Unsolicited notification frame: [0xF0, charId, lenLo, lenHi, ...payload].
  uint8_t header[4];
  header[0] = 0xF0;
  header[1] = charId;
  std::memcpy(&header[2], &len, sizeof(len));
  if (send(clientFd, header, sizeof(header), MSG_NOSIGNAL) < 0 || (len > 0 && send(clientFd, payload, len, MSG_NOSIGNAL) < 0)) {
    CloseClient();
  }
}

void GattBridge::HandleRequest() {
  while (rxLen >= headerSize) {
    uint16_t payloadLen;
    std::memcpy(&payloadLen, &rxBuffer[2], sizeof(payloadLen));
    const size_t total = headerSize + payloadLen;
    if (total > sizeof(rxBuffer)) {
      CloseClient(); // oversized frame: protocol violation
      return;
    }
    if (rxLen < total) {
      return; // wait for the rest
    }

    uint8_t out[kResponseBufferSize];
    uint16_t outLen = 0;
    const uint8_t op = rxBuffer[1];
    const uint8_t status = Dispatch(rxBuffer[0], op, &rxBuffer[4], payloadLen, out, outLen);
    // op 2 = write-without-response: process but send no response frame (the DFU
    // packet char is write-no-rsp; its acks arrive as separate notifications).
    if (op != 2) {
      SendResponse(status, out, outLen);
    }

    std::memmove(rxBuffer, rxBuffer + total, rxLen - total);
    rxLen -= total;
  }
}

uint8_t GattBridge::Forward(Access mode,
                            uint8_t op,
                            uint8_t charByte,
                            uint8_t serviceByte,
                            const std::function<int(ble_gatt_access_ctxt*)>& call,
                            const uint8_t* payload,
                            uint16_t len,
                            uint8_t* out,
                            uint16_t& outLen) {
  const bool write = op == 0;
  const bool read = op == 1;
  const bool ok = (mode == Access::Write && write) || (mode == Access::Read && read) || (mode == Access::WriteRead && (write || read));
  if (!ok) {
    return 0xFE;
  }
  if (write) {
    FakeGattAccess access(BLE_GATT_ACCESS_OP_WRITE_CHR, charByte, const_cast<uint8_t*>(payload), len, serviceByte);
    return static_cast<uint8_t>(call(&access.ctxt));
  }
  // om_pkthdr_len (the field carrying the capacity) is a uint8_t, so the
  // response buffer must stay within its range or the guard would truncate.
  static_assert(kResponseBufferSize <= 255, "response capacity must fit os_mbuf::om_pkthdr_len");
  FakeGattAccess access(BLE_GATT_ACCESS_OP_READ_CHR, charByte, out, 0, serviceByte, kResponseBufferSize);
  const int rc = call(&access.ctxt);
  if (rc != 0) {
    return static_cast<uint8_t>(rc);
  }
  outLen = access.buffer.om_len;
  return 0;
}

uint8_t GattBridge::Dispatch(uint8_t charId, uint8_t op, const uint8_t* payload, uint16_t len, uint8_t* out, uint16_t& outLen) {
  outLen = 0;
  const uint8_t authorization =
    InfiniSim::Ble::VirtualGattAccessPolicy::Authorize(charId, op, systemTask.nimble().IsVirtualLinkAuthenticated());
  if (authorization != 0) {
    return authorization;
  }
  switch (static_cast<CharId>(charId)) {
    // Custom fork services — one line each; Forward builds the fake access,
    // validates the op, routes to the service, and (for reads) captures the
    // response length. Service byte: schedule 0x06, prayer 0x07, beacon 0x08,
    // multi-alarm 0x09, task 0x0a.
    case CharId::ScheduleSync:
      return Forward(
        Access::Write,
        op,
        0x01,
        0x06,
        [&](ble_gatt_access_ctxt* c) {
          return systemTask.nimble().schedule().OnCommand(c);
        },
        payload,
        len,
        out,
        outLen);
    case CharId::ScheduleDigest:
      return Forward(
        Access::Read,
        op,
        0x02,
        0x06,
        [&](ble_gatt_access_ctxt* c) {
          return systemTask.nimble().schedule().OnCommand(c);
        },
        payload,
        len,
        out,
        outLen);
    case CharId::EventRead:
      return Forward(
        Access::WriteRead,
        op,
        0x03,
        0x06,
        [&](ble_gatt_access_ctxt* c) {
          return systemTask.nimble().schedule().OnCommand(c);
        },
        payload,
        len,
        out,
        outLen);

    case CharId::TasksSync:
      return Forward(
        Access::Write,
        op,
        0x01,
        0x0a,
        [&](ble_gatt_access_ctxt* c) {
          return systemTask.nimble().tasks().OnCommand(c);
        },
        payload,
        len,
        out,
        outLen);
    case CharId::TasksDigest:
      return Forward(
        Access::Read,
        op,
        0x02,
        0x0a,
        [&](ble_gatt_access_ctxt* c) {
          return systemTask.nimble().tasks().OnCommand(c);
        },
        payload,
        len,
        out,
        outLen);
    case CharId::TaskRead:
      return Forward(
        Access::WriteRead,
        op,
        0x03,
        0x0a,
        [&](ble_gatt_access_ctxt* c) {
          return systemTask.nimble().tasks().OnCommand(c);
        },
        payload,
        len,
        out,
        outLen);

    case CharId::PrayerSettings:
      return Forward(
        Access::WriteRead,
        op,
        0x01,
        0x07,
        [&](ble_gatt_access_ctxt* c) {
          return systemTask.nimble().prayer().OnCommand(c);
        },
        payload,
        len,
        out,
        outLen);

    case CharId::BeaconKey:
      return Forward(
        Access::WriteRead,
        op,
        0x01,
        0x08,
        [&](ble_gatt_access_ctxt* c) {
          return systemTask.nimble().beacon().OnCommand(c);
        },
        payload,
        len,
        out,
        outLen);
    case CharId::BeaconControl:
      return Forward(
        Access::Write,
        op,
        0x02,
        0x08,
        [&](ble_gatt_access_ctxt* c) {
          return systemTask.nimble().beacon().OnCommand(c);
        },
        payload,
        len,
        out,
        outLen);

    case CharId::MultiAlarm:
      return Forward(
        Access::WriteRead,
        op,
        0x01,
        0x09,
        [&](ble_gatt_access_ctxt* c) {
          return systemTask.nimble().multiAlarm().OnCommand(c);
        },
        payload,
        len,
        out,
        outLen);

    // AlertNotificationService routes on om_data only, so the UUID is irrelevant.
    case CharId::NewAlert:
      return Forward(
        Access::Write,
        op,
        0x00,
        0x06,
        [&](ble_gatt_access_ctxt* c) {
          return systemTask.nimble().alertService().OnAlert(c);
        },
        payload,
        len,
        out,
        outLen);
    // SimpleWeatherService likewise routes on om_data only.
    case CharId::Weather:
      return Forward(
        Access::Write,
        op,
        0x00,
        0x06,
        [&](ble_gatt_access_ctxt* c) {
          return systemTask.nimble().weather().OnCommand(c);
        },
        payload,
        len,
        out,
        outLen);

    case CharId::CurrentTime: {
      // Standard CTS 0x2A2B layout, same parsing as CurrentTimeService.
      if (op != 0 || len < 9) {
        return 0xFE;
      }
      uint16_t year;
      std::memcpy(&year, &payload[0], sizeof(year));
      dateTimeController.SetTime(year, payload[2], payload[3], payload[4], payload[5], payload[6]);
      return 0;
    }

    case CharId::Battery: {
      if (op != 1) {
        return 0xFE;
      }
      out[0] = batteryController.PercentRemaining();
      outLen = 1;
      return 0;
    }

    case CharId::Steps: {
      // MotionService step-count read: today's cumulative steps as uint32 LE.
      if (op != 1) {
        return 0xFE;
      }
      const uint32_t steps = motionController.NbSteps();
      out[0] = steps & 0xFF;
      out[1] = (steps >> 8) & 0xFF;
      out[2] = (steps >> 16) & 0xFF;
      out[3] = (steps >> 24) & 0xFF;
      outLen = 4;
      return 0;
    }

    case CharId::StepsYesterday: {
      // MotionService yesterday-steps read: uint32 LE.
      if (op != 1) {
        return 0xFE;
      }
      const uint32_t steps = motionController.NbSteps(Pinetime::Controllers::MotionController::Days::Yesterday);
      out[0] = steps & 0xFF;
      out[1] = (steps >> 8) & 0xFF;
      out[2] = (steps >> 16) & 0xFF;
      out[3] = (steps >> 24) & 0xFF;
      outLen = 4;
      return 0;
    }

    case CharId::MusicStatus:
    case CharId::MusicArtist:
    case CharId::MusicTrack:
    case CharId::MusicAlbum:
    case CharId::MusicPosition:
    case CharId::MusicTotalLength:
    case CharId::MusicTrackNumber:
    case CharId::MusicTrackTotal:
    case CharId::MusicPlaybackSpeed:
    case CharId::MusicRepeat:
    case CharId::MusicShuffle: {
      // Route both advertised operations through the real firmware callback.
      // Current MusicService accepts reads but appends no bytes, so an honest
      // simulator returns a successful empty payload rather than inventing a
      // readback representation.
      const uint8_t chrByte = 0x02 + (charId - static_cast<uint8_t>(CharId::MusicStatus));
      return Forward(
        Access::WriteRead,
        op,
        chrByte,
        0x00,
        [&](ble_gatt_access_ctxt* context) {
          return systemTask.nimble().music().OnCommand(context);
        },
        payload,
        len,
        out,
        outLen);
    }

    case CharId::DfuControl: {
      // Control-point write (op 0). DfuService routes by attribute handle, not
      // UUID, so the FakeGattAccess UUID is irrelevant — pass 0x1531. Its
      // notifications land on the control point, tagged as DfuControl.
      SimNotify::SetActiveCharId(static_cast<uint8_t>(CharId::DfuControl));
      FakeGattAccess access(BLE_GATT_ACCESS_OP_WRITE_CHR, 0x00, const_cast<uint8_t*>(payload), len);
      return static_cast<uint8_t>(systemTask.nimble().dfu().OnServiceData(0, 0x1531, &access.ctxt));
    }

    case CharId::DfuPacket: {
      // Packet-char write (op 2 = write-no-response): firmware sizes / init
      // packet / 20-byte firmware chunks. Acks arrive as control-point
      // notifications, so tag as DfuControl.
      SimNotify::SetActiveCharId(static_cast<uint8_t>(CharId::DfuControl));
      FakeGattAccess access(BLE_GATT_ACCESS_OP_WRITE_CHR, 0x00, const_cast<uint8_t*>(payload), len);
      return static_cast<uint8_t>(systemTask.nimble().dfu().OnServiceData(0, 0x1532, &access.ctxt));
    }

    case CharId::FsTransfer: {
      // Adafruit BLE filesystem transfer char (0x0200). Routes by handle;
      // notifications tag as FsTransfer.
      SimNotify::SetActiveCharId(static_cast<uint8_t>(CharId::FsTransfer));
      FakeGattAccess access(BLE_GATT_ACCESS_OP_WRITE_CHR, 0x00, const_cast<uint8_t*>(payload), len);
      return static_cast<uint8_t>(systemTask.nimble().fileSystem().OnFSServiceRequested(0, 0x0200, &access.ctxt));
    }

    case CharId::FirmwareRevision: {
      const char* v = Pinetime::Version::VersionString();
      const size_t n = std::strlen(v);
      std::memcpy(out, v, n);
      outLen = static_cast<uint16_t>(n);
      return 0;
    }

    case CharId::CompanionStatus:
      return Forward(
        Access::Read,
        op,
        0x01,
        0x0b,
        [&](ble_gatt_access_ctxt* context) {
          return systemTask.nimble().companionManagement().OnAccess(0x0b01, context);
        },
        payload,
        len,
        out,
        outLen);
    case CharId::CompanionVerify:
      return Forward(
        Access::Read,
        op,
        0x02,
        0x0b,
        [&](ble_gatt_access_ctxt* context) {
          return systemTask.nimble().companionManagement().OnAccess(0x0b02, context);
        },
        payload,
        len,
        out,
        outLen);
    case CharId::FamilyStateStatus:
      if (op != 1) {
        return 0xFE;
      } else {
        const auto status = systemTask.storage().Status().Encode();
        std::memcpy(out, status.data(), status.size());
        outLen = status.size();
        return 0;
      }
  }
  return 0xFF; // unknown characteristic
}

void GattBridge::SendResponse(uint8_t status, const uint8_t* payload, uint16_t len) {
  uint8_t header[3];
  header[0] = status;
  std::memcpy(&header[1], &len, sizeof(len));
  // MSG_NOSIGNAL is belt-and-suspenders alongside the SIG_IGN in Start(): a
  // vanished client yields EPIPE, which we treat as an orderly disconnect
  // rather than crashing the simulator.
  if (send(clientFd, header, sizeof(header), MSG_NOSIGNAL) < 0 || (len > 0 && send(clientFd, payload, len, MSG_NOSIGNAL) < 0)) {
    CloseClient();
  }
}
