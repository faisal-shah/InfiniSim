#pragma once
// Bridge for firmware BLE notifications in the simulator. On hardware, DfuService
// / FSService reply via ble_gattc_notify_custom; the sim stubs that out, so we
// route the payloads here instead. The GATT bridge tags each notification with a
// charId (set from the last DFU/FS write — flows never interleave in the
// single-client sim) and drains the queue to the TCP client as notification
// frames. Thread-safe because DfuService's AsyncSend fires from the SDL timer
// thread.

#include <cstdint>
#include <vector>

namespace SimNotify {
  // Called by the bridge before dispatching a DFU/FS write, so notifications the
  // firmware emits (sync or via the 1 s AsyncSend timer) are tagged correctly.
  // Spontaneous notifications (music/call events) are instead identified by the
  // attribute handle the firmware passed to ble_gattc_notify_custom.
  void SetActiveCharId(uint8_t charId);

  // Called by the sim's ble_gattc_notify_custom stub with the notification bytes
  // and the attribute handle they were sent on.
  void Push(uint16_t attHandle, const uint8_t* data, uint16_t len);

  // Drained by the bridge's poll loop; returns false when the queue is empty.
  // fallbackCharId is the activeCharId captured at push time (DFU/FS flows).
  bool Pop(uint16_t& attHandle, uint8_t& fallbackCharId, std::vector<uint8_t>& bytes);

  // Clear on client disconnect.
  void Clear();
}
