#pragma once

#include "components/ble/BleRadioStateMachine.h"

namespace InfiniSim::Ble {
  constexpr Pinetime::Controllers::BleRadioStateMachine::DesiredMode DesiredModeForBeaconRequest(bool enable, bool radioEnabled) {
    using DesiredMode = Pinetime::Controllers::BleRadioStateMachine::DesiredMode;
    return enable ? DesiredMode::Beacon : (radioEnabled ? DesiredMode::Connectable : DesiredMode::Off);
  }
}
