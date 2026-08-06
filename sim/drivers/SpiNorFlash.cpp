#include "drivers/SpiNorFlash.h"
#include <hal/nrf_gpio.h>
#include <libraries/log/nrf_log.h>
#include "drivers/Spi.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace Pinetime::Drivers;

// Instrumentation: the sim's "flash" is a host file, so it cannot reproduce a
// slow SPI NOR -- but it can count the transactions that make real hardware
// slow. Dump with SIGUSR1.
#include <csignal>
#include <chrono>
#include <thread>
namespace {
  unsigned long flashReads = 0, flashWrites = 0, flashErases = 0;
  unsigned long flashReadBytes = 0, flashWriteBytes = 0;
  volatile sig_atomic_t dumpRequested = 0;
  void OnDumpSignal(int) { dumpRequested = 1; }
  struct DumpInstaller { DumpInstaller() { std::signal(SIGUSR1, OnDumpSignal); } } dumpInstaller;
  void DumpNow() {
    {
      fprintf(stderr, "[flashstats] reads=%lu (%lu B) writes=%lu (%lu B) erases=%lu\n",
              flashReads, flashReadBytes, flashWrites, flashWriteBytes, flashErases);
      fflush(stderr);
    }
  }

  // Serviced by a thread, not by the flash path: a dump requested while the
  // flash is idle must still print, or callers silently read a stale line and
  // measure zero.
  void DumpWatcher() {
    while (true) {
      if (dumpRequested) {
        dumpRequested = 0;
        DumpNow();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  struct DumpThread {
    DumpThread() { std::thread(DumpWatcher).detach(); }
  } dumpThread;
}

SpiNorFlash::SpiNorFlash(const std::string& memoryFilePath) : memoryFilePath {memoryFilePath} {
  namespace fs = std::filesystem;
  fs::path f {memoryFilePath};
  if (fs::exists(f)) {
    memoryFile = std::fstream(memoryFilePath, std::ios::binary | std::fstream::in | std::fstream::out);
    const auto existingSize = fs::file_size(f);
    if (existingSize < memorySize) {
      memoryFile.clear();
      memoryFile.seekp(memorySize - 1);
      memoryFile.put('\0');
      memoryFile.flush();
    }
  } else {
    memoryFile = std::fstream(memoryFilePath, std::ios::trunc | std::ios::binary | std::fstream::in | std::fstream::out);
    memoryFile.seekp(memorySize - 1);
    memoryFile.write("", 1);
  }
}

SpiNorFlash::~SpiNorFlash() {
  if (memoryFile.is_open()) {
    memoryFile.close();
  }
}

void SpiNorFlash::Init() {
  device_id = ReadIdentification();
  NRF_LOG_INFO("[SpiNorFlash] Manufacturer : %d, Memory type : %d, memory density : %d",
               device_id.manufacturer,
               device_id.type,
               device_id.density);
}

void SpiNorFlash::Uninit() {
}

void SpiNorFlash::Sleep() {
  sleeping = true;
  NRF_LOG_INFO("[SpiNorFlash] Sleep")
}

void SpiNorFlash::Wakeup() {
  sleeping = false;
  NRF_LOG_INFO("[SpiNorFlash] Wakeup")
}

void SpiNorFlash::AssertAwake(const char* op) const {
  if (sleeping) {
    // On hardware the chip is in deep power-down here: reads return garbage
    // and writes are ignored. Fail deterministically in the sim instead of
    // letting the corruption go unnoticed. abort() rather than throw: the
    // caller is often littlefs C code, which exceptions must not unwind.
    fprintf(stderr, "SpiNorFlash::%s while flash is asleep - firmware bug (missing wakeup)\n", op);
    abort();
  }
}

SpiNorFlash::Identification SpiNorFlash::ReadIdentification() {
  return {};
}

uint8_t SpiNorFlash::ReadStatusRegister() {
  return 0;
}

bool SpiNorFlash::WriteInProgress() {
  return false;
}

bool SpiNorFlash::WriteEnabled() {
  return false;
}

uint8_t SpiNorFlash::ReadConfigurationRegister() {
  return 0;
}

bool SpiNorFlash::Read(uint32_t address, uint8_t* buffer, size_t size) {
  flashReads++; flashReadBytes += size;
  static_assert(sizeof(uint8_t) == sizeof(char));
  AssertAwake("Read");
  if (address + size * sizeof(uint8_t) > memorySize) {
    throw std::runtime_error("SpiNorFlash::Read out of bounds");
  }
  memoryFile.clear();
  memoryFile.seekg(address);
  memoryFile.read(reinterpret_cast<char*>(buffer), size);
  const bool complete =
    memoryFile.gcount() == static_cast<std::streamsize>(size);
  if (!complete) {
    memoryFile.clear();
  }
  return complete;
}

bool SpiNorFlash::WriteEnable() {
  return true;
}

void SpiNorFlash::SectorErase(uint32_t sectorAddress) {
  flashErases++;
  AssertAwake("SectorErase");
  (void) sectorAddress;
}

uint8_t SpiNorFlash::ReadSecurityRegister() {
  return 0;
}

bool SpiNorFlash::ProgramFailed() {
  return false;
}

bool SpiNorFlash::EraseFailed() {
  return false;
}

SpiNorFlash::Identification SpiNorFlash::GetIdentification() const {
  return device_id;
}

void SpiNorFlash::Write(uint32_t address, const uint8_t* buffer, size_t size) {
  flashWrites++; flashWriteBytes += size;
  AssertAwake("Write");
  if (address + size * sizeof(uint8_t) > memorySize) {
    throw std::runtime_error("SpiNorFlash::Write out of bounds");
  }
  memoryFile.seekp(address);
  memoryFile.write(reinterpret_cast<const char*>(buffer), size);
  memoryFile.flush();
}
