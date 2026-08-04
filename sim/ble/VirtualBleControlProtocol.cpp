#include "ble/VirtualBleControlProtocol.h"

#include <charconv>
#include <sstream>

namespace {
  template <typename Integer>
  bool ParseInteger(std::string_view text, Integer& value, int base = 10) {
    if (base == 0) {
      if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2);
        base = 16;
      } else {
        base = 10;
      }
    }
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value, base);
    return result.ec == std::errc {} && result.ptr == end;
  }

  std::vector<std::string> Tokens(std::string_view line) {
    std::istringstream input {std::string(line)};
    std::vector<std::string> tokens;
    std::string token;
    while (input >> token) {
      tokens.push_back(token);
    }
    return tokens;
  }

  bool ParseAddress(std::string_view text, std::array<uint8_t, 6>& address) {
    if (text.size() != 12) {
      return false;
    }
    for (size_t index = 0; index < address.size(); index++) {
      uint16_t byte = 0;
      if (!ParseInteger(text.substr(index * 2, 2), byte, 16) || byte > 0xff) {
        return false;
      }
      address[index] = static_cast<uint8_t>(byte);
    }
    return true;
  }
}

namespace InfiniSim::Ble {
  bool VirtualControlFramer::Push(std::string_view bytes, std::vector<std::string>& lines) {
    pending.append(bytes);
    if (pending.size() > MaximumLineLength && pending.find('\n') == std::string::npos) {
      pending.clear();
      return false;
    }
    size_t newline = 0;
    while ((newline = pending.find('\n')) != std::string::npos) {
      std::string line = pending.substr(0, newline);
      pending.erase(0, newline + 1);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (!line.empty()) {
        lines.push_back(std::move(line));
      }
    }
    return pending.size() <= MaximumLineLength;
  }

  void VirtualControlFramer::Reset() {
    pending.clear();
  }

  std::optional<VirtualControlCommand> ParseVirtualControlCommand(std::string_view line, std::string& error) {
    const auto tokens = Tokens(line);
    if (tokens.empty()) {
      error = "empty command";
      return std::nullopt;
    }

    VirtualControlCommand command;
    if (tokens[0] == "QUERY" && tokens.size() == 1) {
      command.kind = VirtualControlCommand::Kind::Query;
    } else if (tokens[0] == "CONNECT" && tokens.size() == 1) {
      command.kind = VirtualControlCommand::Kind::Connect;
    } else if (tokens[0] == "DISCONNECT" && tokens.size() == 1) {
      command.kind = VirtualControlCommand::Kind::Disconnect;
    } else if (tokens[0] == "FORCE_CONNECT" && tokens.size() == 1) {
      command.kind = VirtualControlCommand::Kind::ForceConnect;
    } else if (tokens[0] == "FORCE_DISCONNECT" && tokens.size() == 1) {
      command.kind = VirtualControlCommand::Kind::ForceDisconnect;
    } else if (tokens[0] == "DRAIN" && tokens.size() == 1) {
      command.kind = VirtualControlCommand::Kind::Drain;
    } else if (tokens[0] == "REBOOT" && tokens.size() == 1) {
      command.kind = VirtualControlCommand::Kind::Reboot;
    } else if (tokens[0] == "RESET" && tokens.size() == 1) {
      command.kind = VirtualControlCommand::Kind::Reset;
    } else if (tokens[0] == "ADVANCE" && tokens.size() == 2) {
      command.kind = VirtualControlCommand::Kind::Advance;
      if (!ParseInteger(tokens[1], command.value)) {
        error = "ADVANCE requires milliseconds as u32";
        return std::nullopt;
      }
    } else if (tokens[0] == "NEXT_PEER" && (tokens.size() == 4 || tokens.size() == 5)) {
      command.kind = VirtualControlCommand::Kind::NextPeer;
      uint16_t type = 0;
      if (!ParseInteger(tokens[1], type) || type > 3 || !ParseAddress(tokens[2], command.peer.identity.address)) {
        error = "NEXT_PEER requires address type 0..3 and 12 hex digits";
        return std::nullopt;
      }
      command.peer.identity.type = static_cast<uint8_t>(type);
      if (tokens[3] == "UNAUTHENTICATED") {
        command.peer.security = VirtualSecurity::Unauthenticated;
      } else if (tokens[3] == "BONDED") {
        command.peer.security = VirtualSecurity::Bonded;
      } else if (tokens[3] == "AUTHENTICATED") {
        command.peer.security = VirtualSecurity::Authenticated;
      } else {
        error = "NEXT_PEER security must be UNAUTHENTICATED, BONDED, or AUTHENTICATED";
        return std::nullopt;
      }
      if (tokens.size() == 5) {
        if (tokens[4] != "REPLACE") {
          error = "NEXT_PEER optional flag must be REPLACE";
          return std::nullopt;
        }
        command.peer.replaceBond = true;
      }
    } else if (tokens[0] == "GAP_RESULT" && tokens.size() == 4) {
      command.kind = VirtualControlCommand::Kind::GapResult;
      if (tokens[1] == "START") {
        command.gapOperation = VirtualBleAdapter::GapOperation::Start;
      } else if (tokens[1] == "STOP") {
        command.gapOperation = VirtualBleAdapter::GapOperation::Stop;
      } else if (tokens[1] == "TERMINATE") {
        command.gapOperation = VirtualBleAdapter::GapOperation::Terminate;
      } else {
        error = "GAP_RESULT operation must be START, STOP, or TERMINATE";
        return std::nullopt;
      }
      if (!ParseInteger(tokens[2], command.gapResult.code)) {
        error = "GAP_RESULT code must be an integer";
        return std::nullopt;
      }
      if (tokens[3] == "SUCCESS") {
        command.gapResult.classification = VirtualBleAdapter::Radio::Result::Success;
      } else if (tokens[3] == "ALREADY_INACTIVE") {
        command.gapResult.classification = VirtualBleAdapter::Radio::Result::AlreadyInactive;
      } else if (tokens[3] == "ADVERTISING_ACTIVE") {
        command.gapResult.classification = VirtualBleAdapter::Radio::Result::AdvertisingActive;
      } else if (tokens[3] == "FAILED") {
        command.gapResult.classification = VirtualBleAdapter::Radio::Result::Failed;
      } else {
        error = "GAP_RESULT class is invalid";
        return std::nullopt;
      }
    } else if (tokens[0] == "STORE_FAILURE" && tokens.size() == 2) {
      command.kind = VirtualControlCommand::Kind::StoreFailure;
      if (tokens[1] == "NONE") {
        command.storeFailure = VirtualBleAdapter::StoreFailure::None;
      } else if (tokens[1] == "READ") {
        command.storeFailure = VirtualBleAdapter::StoreFailure::Read;
      } else if (tokens[1] == "WRITE") {
        command.storeFailure = VirtualBleAdapter::StoreFailure::Write;
      } else {
        error = "STORE_FAILURE must be NONE, READ, or WRITE";
        return std::nullopt;
      }
    } else if (tokens[0] == "POWER_CUT" && tokens.size() == 2) {
      command.kind = VirtualControlCommand::Kind::PowerCut;
      if (tokens[1] == "NONE") {
        command.powerCut = VirtualBleAdapter::PowerCut::None;
      } else if (tokens[1] == "BEFORE_REPLACE") {
        command.powerCut = VirtualBleAdapter::PowerCut::BeforeReplace;
      } else if (tokens[1] == "AFTER_PARTIAL_STAGED_WRITE") {
        command.powerCut = VirtualBleAdapter::PowerCut::AfterPartialStagedWrite;
      } else if (tokens[1] == "AFTER_STAGED_WRITE") {
        command.powerCut = VirtualBleAdapter::PowerCut::AfterStagedWrite;
      } else {
        error = "POWER_CUT must be NONE, BEFORE_REPLACE, AFTER_PARTIAL_STAGED_WRITE, or AFTER_STAGED_WRITE";
        return std::nullopt;
      }
    } else if (tokens[0] == "CCCD" && tokens.size() == 3) {
      command.kind = VirtualControlCommand::Kind::Cccd;
      uint32_t handle = 0;
      uint32_t flags = 0;
      if (!ParseInteger(tokens[1], handle, 0) || !ParseInteger(tokens[2], flags, 0) || handle > 0xffff || flags > 0xffff) {
        error = "CCCD requires u16 handle and flags";
        return std::nullopt;
      }
      command.handle = static_cast<uint16_t>(handle);
      command.flags = static_cast<uint16_t>(flags);
    } else {
      error = "unknown command or argument count";
      return std::nullopt;
    }
    return command;
  }
}
