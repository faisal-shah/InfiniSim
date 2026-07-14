#include "gatt_bridge.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "components/ble/ScheduleService.h"
#include "components/ble/AlertNotificationService.h"
#include "components/battery/BatteryController.h"
#include "components/datetime/DateTimeController.h"
#include "systemtask/SystemTask.h"

namespace {
  constexpr size_t headerSize = 4; // charId u8, op u8, len u16

  // Build a fake single-buffer mbuf + access context around `data`.
  struct FakeGattAccess {
    os_mbuf buffer {};
    ble_gatt_chr_def chrDef {};
    ble_uuid128_t uuid {};
    ble_gatt_access_ctxt ctxt {};

    FakeGattAccess(uint8_t op, uint8_t charIdByte, uint8_t* data, uint16_t len) {
      uuid = ble_uuid128_t {.u = {.type = BLE_UUID_TYPE_128},
                            .value = {0xd0, 0x42, 0x19, 0x3a, 0x3b, 0x43, 0x23, 0x8e, 0xfe, 0x48, 0xfc, 0x78, charIdByte, 0x00, 0x06, 0x00}};
      chrDef.uuid = &uuid.u;
      buffer.om_data = data;
      buffer.om_len = op == BLE_GATT_ACCESS_OP_WRITE_CHR ? len : 0;
      ctxt.op = op;
      ctxt.om = &buffer;
      ctxt.chr = &chrDef;
    }
  };
}

GattBridge::GattBridge(Pinetime::System::SystemTask& systemTask,
                       Pinetime::Controllers::DateTime& dateTimeController,
                       Pinetime::Controllers::Battery& batteryController)
  : systemTask {systemTask}, dateTimeController {dateTimeController}, batteryController {batteryController} {
}

GattBridge::~GattBridge() {
  CloseClient();
  if (listenFd >= 0) {
    close(listenFd);
  }
}

bool GattBridge::Start(uint16_t port) {
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
  if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(listenFd, 1) != 0) {
    perror("gatt-bridge bind/listen");
    close(listenFd);
    listenFd = -1;
    return false;
  }
  printf("InfiniSim: GATT bridge listening on port %u\n", port);
  return true;
}

void GattBridge::CloseClient() {
  if (clientFd >= 0) {
    close(clientFd);
    clientFd = -1;
    rxLen = 0;
    // A dropped companion connection aborts any open sync transaction, exactly
    // like a BLE disconnect.
    systemTask.nimble().schedule().OnDisconnect();
  }
}

void GattBridge::Poll() {
  if (listenFd < 0) {
    return;
  }

  // A new connection replaces any existing client, mirroring how a BLE
  // reconnect supersedes the old link. This also prevents a wedged client
  // from blocking the bridge forever.
  const int incoming = accept4(listenFd, nullptr, nullptr, SOCK_NONBLOCK);
  if (incoming >= 0) {
    CloseClient();
    clientFd = incoming;
    const int one = 1;
    setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    rxLen = 0;
  }
  if (clientFd < 0) {
    return;
  }

  while (true) {
    const ssize_t n = read(clientFd, rxBuffer + rxLen, sizeof(rxBuffer) - rxLen);
    if (n > 0) {
      rxLen += n;
      HandleRequest();
      continue;
    }
    if (n == 0) {
      CloseClient(); // orderly shutdown
    }
    return; // EAGAIN or closed
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

    uint8_t out[64];
    uint16_t outLen = 0;
    const uint8_t status = Dispatch(rxBuffer[0], rxBuffer[1], &rxBuffer[4], payloadLen, out, outLen);
    SendResponse(status, out, outLen);

    std::memmove(rxBuffer, rxBuffer + total, rxLen - total);
    rxLen -= total;
  }
}

uint8_t GattBridge::Dispatch(uint8_t charId, uint8_t op, const uint8_t* payload, uint16_t len, uint8_t* out, uint16_t& outLen) {
  outLen = 0;
  switch (static_cast<CharId>(charId)) {
    case CharId::ScheduleSync: {
      if (op != 0) {
        return 0xFE;
      }
      FakeGattAccess access(BLE_GATT_ACCESS_OP_WRITE_CHR, 0x01, const_cast<uint8_t*>(payload), len);
      return static_cast<uint8_t>(systemTask.nimble().schedule().OnCommand(&access.ctxt));
    }

    case CharId::ScheduleDigest: {
      if (op != 1) {
        return 0xFE;
      }
      FakeGattAccess access(BLE_GATT_ACCESS_OP_READ_CHR, 0x02, out, 0);
      const int rc = systemTask.nimble().schedule().OnCommand(&access.ctxt);
      if (rc != 0) {
        return static_cast<uint8_t>(rc);
      }
      outLen = access.buffer.om_len;
      return 0;
    }

    case CharId::CurrentTime: {
      // Standard CTS 0x2A2B layout, same parsing as CurrentTimeService.
      if (op != 0 || len < 9) {
        return 0xFE;
      }
      uint16_t year;
      std::memcpy(&year, &payload[0], sizeof(year));
      dateTimeController.SetTime(year, payload[2], payload[3], payload[4], payload[5], payload[6]);
      printf("InfiniSim: bridge set time to %04u-%02u-%02u %02u:%02u:%02u\n",
             year,
             payload[2],
             payload[3],
             payload[4],
             payload[5],
             payload[6]);
      return 0;
    }

    case CharId::NewAlert: {
      if (op != 0) {
        return 0xFE;
      }
      FakeGattAccess access(BLE_GATT_ACCESS_OP_WRITE_CHR, 0x00, const_cast<uint8_t*>(payload), len);
      return static_cast<uint8_t>(systemTask.nimble().alertService().OnAlert(&access.ctxt));
    }

    case CharId::Battery: {
      if (op != 1) {
        return 0xFE;
      }
      out[0] = batteryController.PercentRemaining();
      outLen = 1;
      return 0;
    }
  }
  return 0xFF; // unknown characteristic
}

void GattBridge::SendResponse(uint8_t status, const uint8_t* payload, uint16_t len) {
  uint8_t header[3];
  header[0] = status;
  std::memcpy(&header[1], &len, sizeof(len));
  // Best effort: the socket is non-blocking but responses are tiny.
  if (write(clientFd, header, sizeof(header)) < 0 || (len > 0 && write(clientFd, payload, len) < 0)) {
    CloseClient();
  }
}
