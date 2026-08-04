#pragma once

#include "generated/CompanionProtocolMetadata.h"
#include "host/ble_att.h"

#include <cstdint>

namespace InfiniSim::Ble {
  class VirtualGattAccessPolicy {
  public:
    static constexpr uint8_t BadOperation = 0xfe;
    static constexpr uint8_t UnknownCharacteristic = 0xff;

    static uint8_t Authorize(uint8_t characteristicId, uint8_t operation, bool authenticated) {
      const auto* metadata = SimCompanionProtocol::Metadata(characteristicId);
      if (metadata == nullptr) {
        return UnknownCharacteristic;
      }
      if (metadata->authenticated && !authenticated) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
      }

      uint8_t required = 0;
      switch (operation) {
        case 0:
          required = SimCompanionProtocol::Write;
          break;
        case 1:
          required = SimCompanionProtocol::Read;
          break;
        case 2:
          required = SimCompanionProtocol::WriteWithoutResponse;
          break;
        default:
          return BadOperation;
      }
      return (metadata->access & required) != 0 ? 0 : BadOperation;
    }
  };
}
