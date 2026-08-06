message(STATUS "Using CMake version ${CMAKE_VERSION}")
cmake_minimum_required(VERSION 3.12...${CMAKE_VERSION})

SET(InfiniTime_DIR "${CMAKE_SOURCE_DIR}/InfiniTime" CACHE PATH "Path to InfiniTime source path to use for simulator")

if(NOT EXISTS ${InfiniTime_DIR})
  message(FATAL_ERROR "Can't access folder '${InfiniTime_DIR}'. Try `git submodule update --init --recursive` to initialize the git submodule of InfiniTime")
endif()
if(NOT EXISTS ${InfiniTime_DIR}/src/libs/lvgl)
  message(FATAL_ERROR "Can't access folder '${InfiniTime_DIR}/src/libs/lvgl'. Try `git submodule update --init --recursive` to initialize the git submodule of lvgl inside InfiniTime")
endif()

set(INFINITIME_FAMILY_REQUIRED_FILES
  protocol/companion.json
  src/components/ble/BleRadioStateMachine.cpp
  src/components/ble/BondBootPersistenceGate.h
  src/components/ble/BondRegistry.cpp
  src/components/ble/BondStorePolicy.cpp
  src/components/ble/BondStoreCodec.cpp
  src/components/ble/BondPersistenceCoordinator.cpp
  src/components/ble/CompanionManagementStatus.h
  src/components/ble/CompanionManagementStatus.cpp
  src/components/ble/CompanionManagementService.h
  src/components/ble/CompanionManagementService.cpp
)
foreach(required_file IN LISTS INFINITIME_FAMILY_REQUIRED_FILES)
  if(NOT EXISTS "${InfiniTime_DIR}/${required_file}")
    message(FATAL_ERROR
      "InfiniTime_DIR='${InfiniTime_DIR}' is not a family-feature tree: missing ${required_file}. "
      "Use the faisal-shah/InfiniTime family-features branch.")
  endif()
endforeach()

# try to extract the PineTime project version
file(READ "${InfiniTime_DIR}/CMakeLists.txt" main_cmake)
string(REGEX MATCH "project\\(pinetime VERSION ([0-9]*\.[0-9]*\.[0-9]*)" _ ${main_cmake})
set(PROJECT_VERSION ${CMAKE_MATCH_1})
message(STATUS "InfiniTime PROJECT_VERSION extracted: ${PROJECT_VERSION}")

project(InfiniSim VERSION ${PROJECT_VERSION} LANGUAGES C CXX)
# https://cmake.org/cmake/help/latest/prop_tgt/CXX_STANDARD.html
string(COMPARE EQUAL "${CMAKE_CXX_STANDARD}" "" no_cmake_cxx_standard_set)
if(no_cmake_cxx_standard_set)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  message(STATUS "Using default C++ standard ${CMAKE_CXX_STANDARD}")
else()
  message(STATUS "Using user specified C++ standard ${CMAKE_CXX_STANDARD}")
endif()
set(CMAKE_C_STANDARD 11)#C11

# Match ARM char semantics: on the watch (AAPCS) plain `char` is UNSIGNED, and
# firmware code relies on it (e.g. MusicService parses big-endian ints via
# char* — byte 0xF0 must read as 240, not -16). x86 defaults to signed char,
# which made shared firmware code behave differently in the sim.
add_compile_options(-funsigned-char)

# copy lv_conf.h from InfiniTime project and do little modifications
file(READ "${InfiniTime_DIR}/src/libs/lv_conf.h" lv_conf_main)
# set '#define LV_TICK_CUSTOM     0'
string(REGEX REPLACE "#define[ ]+LV_TICK_CUSTOM[ ]+1" "#define LV_TICK_CUSTOM     0" lv_conf_main "${lv_conf_main}")
# allow more memory in debug mode for the simulator
#define LV_MEM_SIZE    (14U * 1024U)
string(REGEX REPLACE "#define[ ]+LV_MEM_SIZE[ ]+\\([^\)]*\\)"
  "#ifdef NDEBUG
#define LV_MEM_SIZE    (14U * 1024U)
#else // debug mode -> allow more memory
#define LV_MEM_SIZE    (28U * 1024U)
#endif"
  lv_conf_main "${lv_conf_main}")
# write to temporary file and overwrite file to use only if lv_conf changed to keep useless recompiles
# at a minimum
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/lv_conf_modified.h" "${lv_conf_main}")
configure_file(
  "${CMAKE_CURRENT_BINARY_DIR}/lv_conf_modified.h"
  "${CMAKE_CURRENT_BINARY_DIR}/lv_conf.h"
  COPYONLY)

file(GLOB_RECURSE LVGL_INCLUDES "lv_drivers/*.h" "${InfiniTime_DIR}/src/libs/lvgl/src/*.h"  "./*.h" )
file(GLOB_RECURSE LVGL_SOURCES  "lv_drivers/*.c" "${InfiniTime_DIR}/src/libs/lvgl/src/*.c" )

add_library(sim-base STATIC
  # LVGL sources
  ${LVGL_SOURCES} ${LVGL_INCLUDES}
  # FreeRTOS
  sim/FreeRTOS.h
  sim/FreeRTOS.cpp
  sim/task.h
  sim/task.cpp
  sim/timers.h
  sim/timers.cpp
  sim/queue.h
  sim/queue.cpp
  sim/semphr.h
  sim/semphr.cpp
  # src/FreeRTOS
  sim/portmacro_cmsis.h
  sim/portmacro_cmsis.cpp
  # nrf
  sim/libraries/log/nrf_log.h
  sim/libraries/delay/nrf_delay.h
  sim/libraries/delay/nrf_delay.cpp
  sim/nrfx/nrfx_log.h
  sim/nrfx/drivers/include/nrfx_twi.h
  sim/nrfx/hal/nrf_gpio.h
  sim/nrfx/hal/nrf_gpio.cpp
  sim/nrfx/hal/nrfx_gpiote.h # includes hal/nrf_gpio.h
  sim/nrfx/hal/nrf_rtc.h
  sim/nrfx/hal/nrf_rtc.cpp
  sim/nrfx/mdk/nrf.h
  sim/nrfx/mdk/nrf52.h
  sim/nrfx/mdk/nrf52.cpp
  sim/nrfx/mdk/nrf52_bitfields.h
  # nrf/components/libraries/timer
  sim/libraries/gpiote/app_gpiote.h # includes hal/nrf_gpio.h
  # nibmle
  sim/host/ble_att.h
  sim/host/ble_gap.h
  sim/host/ble_gatt.h
  sim/host/ble_gatt.cpp
  sim/host/ble_hs_mbuf.h
  sim/host/ble_hs_mbuf.cpp
  sim/host/ble_uuid.h
  sim/host/ble_uuid.cpp
  sim/host/os_mbuf.h
  sim/host/os_mbuf.cpp
)
# include the generated lv_conf.h file before anything else
target_include_directories(sim-base PUBLIC "${CMAKE_CURRENT_BINARY_DIR}") # lv_conf.h
target_include_directories(sim-base PUBLIC "sim")
target_include_directories(sim-base PUBLIC "sim/libraries/log") # for nrf_log.h
target_include_directories(sim-base PUBLIC "sim/nrfx") # for nrfx_log.h and others
target_include_directories(sim-base PUBLIC "sim/nrfx/hal") # for nrfx_log.h
target_include_directories(sim-base PUBLIC "sim/nrfx/mdk") # for nrf52_bitfields.h

target_include_directories(sim-base PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}") # lv_drv_conf.h
target_include_directories(sim-base PUBLIC "${InfiniTime_DIR}/src/libs")
target_include_directories(sim-base PUBLIC "lv_drivers")

target_include_directories(sim-base PUBLIC "${InfiniTime_DIR}/src") # InfiniTime drivers, components and all

find_package(SDL2 REQUIRED)
message(STATUS "Found SDL2 version ${SDL2_VERSION}")
target_link_libraries(sim-base PUBLIC SDL2::SDL2)

# Compile the portable BLE policy directly from the selected InfiniTime tree.
# The simulator owns only deterministic ports around these classes; it does not
# carry a second radio, bond, or persistence policy implementation.
add_library(infinitime-ble-portable STATIC
  ${InfiniTime_DIR}/src/components/ble/BleRadioStateMachine.cpp
  ${InfiniTime_DIR}/src/components/ble/BondRegistry.cpp
  ${InfiniTime_DIR}/src/components/ble/BondStorePolicy.cpp
  ${InfiniTime_DIR}/src/components/ble/BondStoreCodec.cpp
  ${InfiniTime_DIR}/src/components/ble/BondPersistenceCoordinator.cpp
  ${InfiniTime_DIR}/src/components/ble/CompanionManagementStatus.cpp
)
target_include_directories(infinitime-ble-portable PUBLIC "${InfiniTime_DIR}/src")
target_compile_features(infinitime-ble-portable PUBLIC cxx_std_20)

add_library(infinisim-ble-adapters STATIC
  sim/ble/VirtualBleAdapter.cpp
  sim/ble/VirtualBleControlProtocol.cpp
)
target_include_directories(infinisim-ble-adapters PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/sim"
  "${InfiniTime_DIR}/src"
)
target_link_libraries(infinisim-ble-adapters PUBLIC infinitime-ble-portable)
target_compile_features(infinisim-ble-adapters PUBLIC cxx_std_20)

file(SHA256 "${InfiniTime_DIR}/protocol/companion.json" COMPANION_MANIFEST_SHA256)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sim/generated/CompanionProtocolMetadata.h" COMPANION_SIM_METADATA)
string(FIND "${COMPANION_SIM_METADATA}" "${COMPANION_MANIFEST_SHA256}" COMPANION_METADATA_DIGEST_OFFSET)
if(COMPANION_METADATA_DIGEST_OFFSET EQUAL -1)
  message(FATAL_ERROR "sim/generated/CompanionProtocolMetadata.h is stale for ${InfiniTime_DIR}/protocol/companion.json")
endif()

set(MONITOR_ZOOM 1 CACHE STRING "Scale simulator window by this factor")
if(MONITOR_ZOOM MATCHES "^[0-9]\.?[0-9]*")
  message(STATUS "Using MONITOR_ZOOM=${MONITOR_ZOOM}")
  target_compile_definitions(sim-base PUBLIC MONITOR_ZOOM=${MONITOR_ZOOM})
else()
  message(FATAL_ERROR "variable MONITOR_ZOOM=${MONITOR_ZOOM} must be a positive number")
endif()

add_executable(infinisim main.cpp)
# add simulator files
target_sources(infinisim PUBLIC
  sim/displayapp/LittleVgl.h
  sim/displayapp/LittleVgl.cpp
  sim/components/battery/BatteryController.h
  sim/components/battery/BatteryController.cpp
  sim/components/ble/NimbleController.h
  sim/components/ble/NimbleController.cpp
  sim/displayapp/LvglGuard.h
  sim/displayapp/LvglGuard.cpp
  sim/gatt_bridge.h
  sim/gatt_bridge.cpp
  ${InfiniTime_DIR}/src/components/ble/CompanionManagementService.h
  ${InfiniTime_DIR}/src/components/ble/CompanionManagementService.cpp
  sim/components/brightness/BrightnessController.h
  sim/components/brightness/BrightnessController.cpp
  sim/components/firmwarevalidator/FirmwareValidator.h
  sim/components/firmwarevalidator/FirmwareValidator.cpp
  sim/components/heartrate/HeartRateController.h
  sim/components/heartrate/HeartRateController.cpp
  sim/drivers/Bma421.h
  sim/drivers/Bma421.cpp
  sim/drivers/Cst816s.h
  sim/drivers/Cst816s.cpp
  sim/drivers/SpiMaster.h
  sim/drivers/SpiMaster.cpp
  sim/drivers/TwiMaster.h
  sim/drivers/TwiMaster.cpp
  sim/drivers/SpiNorFlash.h
  sim/drivers/SpiNorFlash.cpp
  sim/heartratetask/HeartRateTask.h
  sim/heartratetask/HeartRateTask.cpp
)
target_link_libraries(infinisim PUBLIC sim-base infinitime-ble-portable infinisim-ble-adapters)

option(ENABLE_BLE_TEST_CONTROL "Build the loopback-only virtual BLE test-control endpoint" ON)
if(ENABLE_BLE_TEST_CONTROL)
  target_sources(infinisim PRIVATE
    sim/ble/VirtualBleControlServer.h
    sim/ble/VirtualBleControlServer.cpp
  )
  target_compile_definitions(infinisim PRIVATE INFINISIM_ENABLE_BLE_TEST_CONTROL=1)
endif()

# add Screens, fonts and icons with a globbing expression,
# to enable easier CI test-runs for PRs adding new Screens/Fonts/Icons
file(GLOB InfiniTime_SCREENS
  "${InfiniTime_DIR}/src/displayapp/screens/*.h"
  "${InfiniTime_DIR}/src/displayapp/screens/*.cpp"
  "${InfiniTime_DIR}/src/displayapp/screens/settings/*.h"
  "${InfiniTime_DIR}/src/displayapp/screens/settings/*.cpp"
)
file(GLOB InfiniTime_FONTS
  "${InfiniTime_DIR}/src/displayapp/fonts/*.c"
)
file(GLOB InfiniTime_ICONS
  "${InfiniTime_DIR}/src/displayapp/icons/*.c"
)
file(GLOB InfiniTime_WIDGETS
  "${InfiniTime_DIR}/src/displayapp/widgets/*.cpp"
  "${InfiniTime_DIR}/src/displayapp/widgets/*.h"
)
target_sources(infinisim PUBLIC ${InfiniTime_SCREENS})
target_sources(infinisim PUBLIC ${InfiniTime_FONTS})
target_sources(infinisim PUBLIC ${InfiniTime_ICONS})
target_sources(infinisim PUBLIC ${InfiniTime_WIDGETS})

# add files directly from InfiniTime sources
target_sources(infinisim PUBLIC
  ${InfiniTime_DIR}/src/BootloaderVersion.h
  ${InfiniTime_DIR}/src/BootloaderVersion.cpp
  ${InfiniTime_DIR}/src/displayapp/Colors.h
  ${InfiniTime_DIR}/src/displayapp/Colors.cpp
  ${InfiniTime_DIR}/src/displayapp/DisplayApp.h
  ${InfiniTime_DIR}/src/displayapp/DisplayApp.cpp
  ${InfiniTime_DIR}/src/buttonhandler/ButtonHandler.h
  ${InfiniTime_DIR}/src/buttonhandler/ButtonHandler.cpp
  ${InfiniTime_DIR}/src/components/stopwatch/StopWatchController.h
  ${InfiniTime_DIR}/src/components/stopwatch/StopWatchController.cpp
  ${InfiniTime_DIR}/src/components/multialarm/MultiAlarmController.h
  ${InfiniTime_DIR}/src/components/multialarm/MultiAlarmController.cpp
  ${InfiniTime_DIR}/src/components/schedule/ScheduleController.h
  ${InfiniTime_DIR}/src/components/schedule/ScheduleController.cpp
  ${InfiniTime_DIR}/src/components/task/TaskController.h
  ${InfiniTime_DIR}/src/components/task/TaskController.cpp
  ${InfiniTime_DIR}/src/components/prayer/PrayerRules.h
  ${InfiniTime_DIR}/src/components/prayer/PrayerController.h
  ${InfiniTime_DIR}/src/components/prayer/PrayerController.cpp
  ${InfiniTime_DIR}/src/components/beacon/BeaconRules.h
  ${InfiniTime_DIR}/src/components/beacon/BeaconController.h
  ${InfiniTime_DIR}/src/components/beacon/BeaconController.cpp
  ${InfiniTime_DIR}/src/components/alertqueue/AlertQueue.cpp
  ${InfiniTime_DIR}/src/components/ble/BleController.h
  ${InfiniTime_DIR}/src/components/ble/BleController.cpp
  ${InfiniTime_DIR}/src/components/datetime/DateTimeController.h
  ${InfiniTime_DIR}/src/components/datetime/DateTimeController.cpp
  ${InfiniTime_DIR}/src/components/settings/Settings.h
  ${InfiniTime_DIR}/src/components/settings/Settings.cpp
  ${InfiniTime_DIR}/src/components/ble/AlertNotificationService.h
  ${InfiniTime_DIR}/src/components/ble/AlertNotificationService.cpp
  ${InfiniTime_DIR}/src/components/ble/MotionService.h
  ${InfiniTime_DIR}/src/components/ble/MotionService.cpp
  ${InfiniTime_DIR}/src/components/ble/MusicService.h
  ${InfiniTime_DIR}/src/components/ble/MusicService.cpp
  ${InfiniTime_DIR}/src/components/ble/NavigationService.h
  ${InfiniTime_DIR}/src/components/ble/NavigationService.cpp
  ${InfiniTime_DIR}/src/components/ble/NotificationManager.h
  ${InfiniTime_DIR}/src/components/ble/NotificationManager.cpp
  ${InfiniTime_DIR}/src/components/ble/SimpleWeatherService.h
  ${InfiniTime_DIR}/src/components/ble/SimpleWeatherService.cpp
  ${InfiniTime_DIR}/src/components/ble/ScheduleService.h
  ${InfiniTime_DIR}/src/components/ble/ScheduleService.cpp
  ${InfiniTime_DIR}/src/components/ble/SyncWakeLock.h
  ${InfiniTime_DIR}/src/components/ble/SyncWakeLock.cpp
  ${InfiniTime_DIR}/src/components/ble/TaskService.h
  ${InfiniTime_DIR}/src/components/ble/TaskService.cpp
  ${InfiniTime_DIR}/src/components/ble/PrayerService.h
  ${InfiniTime_DIR}/src/components/ble/PrayerService.cpp
  ${InfiniTime_DIR}/src/components/ble/MultiAlarmService.cpp
  ${InfiniTime_DIR}/src/components/ble/BeaconService.h
  ${InfiniTime_DIR}/src/components/ble/BeaconService.cpp
  ${InfiniTime_DIR}/src/components/ble/DfuService.cpp
  ${InfiniTime_DIR}/src/components/ble/FSService.cpp
  sim/notify_queue.cpp
  ${InfiniTime_DIR}/src/components/fs/FS.h
  ${InfiniTime_DIR}/src/components/fs/FS.cpp
  ${InfiniTime_DIR}/src/components/motion/MotionController.h
  ${InfiniTime_DIR}/src/components/motion/MotionController.cpp
  ${InfiniTime_DIR}/src/components/motor/MotorController.h
  ${InfiniTime_DIR}/src/components/motor/MotorController.cpp
  ${InfiniTime_DIR}/src/drivers/Hrs3300.h
  ${InfiniTime_DIR}/src/drivers/Hrs3300.cpp
  ${InfiniTime_DIR}/src/drivers/PinMap.h
  ${InfiniTime_DIR}/src/drivers/Spi.h
  ${InfiniTime_DIR}/src/drivers/Spi.cpp
  ${InfiniTime_DIR}/src/drivers/St7789.h
  ${InfiniTime_DIR}/src/drivers/St7789.cpp
  ${InfiniTime_DIR}/src/drivers/Watchdog.h
  ${InfiniTime_DIR}/src/drivers/Watchdog.cpp
  ${InfiniTime_DIR}/src/touchhandler/TouchHandler.h
  ${InfiniTime_DIR}/src/touchhandler/TouchHandler.cpp
  ${InfiniTime_DIR}/src/systemtask/SystemTask.h
  ${InfiniTime_DIR}/src/systemtask/SystemTask.cpp
  ${InfiniTime_DIR}/src/systemtask/SystemMonitor.h
  ${InfiniTime_DIR}/src/systemtask/SystemMonitor.cpp
  ${InfiniTime_DIR}/src/storagetask/StorageTask.h
  ${InfiniTime_DIR}/src/storagetask/StorageTask.cpp
  ${InfiniTime_DIR}/src/components/ble/FamilyStateStatus.h
  ${InfiniTime_DIR}/src/components/ble/FamilyStateStatus.cpp
  ${InfiniTime_DIR}/src/components/fs/FamilyStateCodec.h
  ${InfiniTime_DIR}/src/components/fs/FamilyStateCodec.cpp
  ${InfiniTime_DIR}/src/utility/Math.h
  ${InfiniTime_DIR}/src/utility/Math.cpp
)

target_sources(infinisim PUBLIC
  ${InfiniTime_DIR}/src/displayapp/InfiniTimeTheme.cpp
  ${InfiniTime_DIR}/src/displayapp/InfiniTimeTheme.h
)

if(EXISTS ${InfiniTime_DIR}/src/components/timer/Timer.h)
  target_sources(infinisim PUBLIC
    ${InfiniTime_DIR}/src/components/timer/Timer.h
    ${InfiniTime_DIR}/src/components/timer/Timer.cpp
  )
else()
  target_compile_definitions(infinisim PUBLIC INFINITIME_TIMERCONTROLLER)
  target_sources(infinisim PUBLIC
    ${InfiniTime_DIR}/src/components/timer/TimerController.h
    ${InfiniTime_DIR}/src/components/timer/TimerController.cpp
  )
endif()

if(EXISTS ${InfiniTime_DIR}/src/systemtask/WakeLock.h)
  target_sources(infinisim PUBLIC
    ${InfiniTime_DIR}/src/systemtask/WakeLock.h
    ${InfiniTime_DIR}/src/systemtask/WakeLock.cpp
  )
endif()

# littlefs
add_library(littlefs STATIC
  ${InfiniTime_DIR}/src/libs/littlefs/lfs_util.h
  ${InfiniTime_DIR}/src/libs/littlefs/lfs.h
  ${InfiniTime_DIR}/src/libs/littlefs/lfs_util.c
  ${InfiniTime_DIR}/src/libs/littlefs/lfs.c
)
target_include_directories(littlefs PUBLIC "${InfiniTime_DIR}/src/libs/littlefs")
target_link_libraries(infinisim PRIVATE littlefs)

# check version number of installed node package for minimum required
find_program(NODE_EXE "node" NO_CACHE QUIET)
if(NODE_EXE)
  execute_process(COMMAND "${NODE_EXE}" "--version" OUTPUT_VARIABLE NODE_VERSION)
  string(REPLACE "v" "" NODE_VERSION "${NODE_VERSION}")
  if(NODE_VERSION VERSION_LESS 14.0.0)
    message(WARNING "Node version v${NODE_VERSION} is less than required 14+, you will probably encounter build errors")
  else()
    message(STATUS "Founde node version v${NODE_VERSION}")
  endif()
else()
  message(STATUS "Didn't find executable 'node' in path to check its version number")
endif()

if(EXISTS ${InfiniTime_DIR}/src/displayapp/fonts/CMakeLists.txt)
  # available since https://github.com/InfiniTimeOrg/InfiniTime/pull/1097
  message(STATUS "add subdirectory ${InfiniTime_DIR}/src/displayapp/fonts for 'infinitime_fonts' target")
  add_subdirectory(${InfiniTime_DIR}/src/displayapp/fonts fonts)
  target_link_libraries(infinisim PRIVATE infinitime_fonts)
endif()

if(EXISTS ${InfiniTime_DIR}/src/displayapp/apps/CMakeLists.txt)
  # available since https://github.com/InfiniTimeOrg/InfiniTime/pull/1928
  message(STATUS "add subdirectory ${InfiniTime_DIR}/src/displayapp/apps for 'infinitime_apps' target")
  add_subdirectory(${InfiniTime_DIR}/src/displayapp/apps displayapp/apps)
  target_link_libraries(infinisim PRIVATE infinitime_apps)
endif()

option(BUILD_RESOURCES "Generate a resource.zip file to install to spi.raw file" ON)
if(BUILD_RESOURCES)
  if(EXISTS ${InfiniTime_DIR}/src/resources/CMakeLists.txt)
    # set verison variables as used in resources/CMakeLists.txt
    set(pinetime_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(pinetime_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(pinetime_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    # available since https://github.com/InfiniTimeOrg/InfiniTime/pull/1097
    message(STATUS "add subdirectory ${InfiniTime_DIR}/src/resources for 'GenerateResources' target")
    add_subdirectory(${InfiniTime_DIR}/src/resources resources)
    add_dependencies(infinisim GenerateResources)
  else()
    message(FATAL_ERROR "BUILD_RESOURCES is enabled, but InfiniTime subdirectory has no resources/CMakeLists.txt file")
  endif()
endif()


list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Some toolchains (e.g. g++-8) require to explicitly link with the standard filesystem library
# See https://github.com/InfiniTimeOrg/InfiniSim/issues/57#issuecomment-1386889378
find_package(Filesystem REQUIRED)
target_link_libraries(infinisim PRIVATE std::filesystem)

# Get the latest abbreviated commit hash of the working branch
find_package(Git REQUIRED)
execute_process(
  COMMAND ${GIT_EXECUTABLE} log -1 --format=%h
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE PROJECT_GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(VERSION_EDIT_WARNING "// Do not edit this file, it is automatically generated by CMAKE!")
configure_file("${InfiniTime_DIR}/src/Version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/Version.h")

option(WITH_PNG "Compile with libpng support to dump current screen as png" ON)
if(WITH_PNG)
  find_package(PNG REQUIRED)
  target_compile_definitions(infinisim PRIVATE WITH_PNG)
  target_link_libraries(infinisim PRIVATE PNG::PNG)
endif()

target_include_directories(infinisim PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/external/gif-h")

# embed background image for the status window to use
add_subdirectory(img)
add_dependencies(infinisim infinisim_img_background)

install(TARGETS infinisim DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
  add_executable(infinisim-ble-adapters-test
    tests/BleAdaptersTest.cpp
  )
  target_include_directories(infinisim-ble-adapters-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/sim"
    "${InfiniTime_DIR}/src"
  )
  target_link_libraries(infinisim-ble-adapters-test PRIVATE infinisim-ble-adapters)
  target_compile_features(infinisim-ble-adapters-test PRIVATE cxx_std_20)
  add_test(NAME infinisim-ble-adapters COMMAND infinisim-ble-adapters-test)
  set_tests_properties(infinisim-ble-adapters PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  if(ENABLE_BLE_TEST_CONTROL)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME infinisim-gatt-bridge-smoke
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/GattBridgeSmoke.py" "$<TARGET_FILE:infinisim>"
    )
    set_tests_properties(infinisim-gatt-bridge-smoke PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
  endif()
endif()

# helper library to manipulate littlefs raw image
add_executable(littlefs-do
  littlefs-do-main.cpp
  # want to use FS.h
  ${InfiniTime_DIR}/src/components/fs/FS.h
  ${InfiniTime_DIR}/src/components/fs/FS.cpp
  # dependencies for FS.h
  sim/drivers/SpiNorFlash.h
  sim/drivers/SpiNorFlash.cpp
  # dependencies for SpiNorFlash.h
  ${InfiniTime_DIR}/src/drivers/Spi.h
  ${InfiniTime_DIR}/src/drivers/Spi.cpp
  sim/drivers/SpiMaster.h
  sim/drivers/SpiMaster.cpp

  ${InfiniTime_DIR}/src/components/settings/Settings.h
  ${InfiniTime_DIR}/src/components/fs/FamilyState.h
  ${InfiniTime_DIR}/src/components/fs/FamilyStateCodec.h
  ${InfiniTime_DIR}/src/components/fs/FamilyStateCodec.cpp
)

target_link_libraries(littlefs-do PUBLIC sim-base)
target_link_libraries(littlefs-do PUBLIC littlefs)

target_link_libraries(littlefs-do PRIVATE infinitime_fonts)

add_subdirectory(external/miniz)
add_subdirectory(external/nlohmann_json)
target_link_libraries(littlefs-do PRIVATE miniz)
target_link_libraries(littlefs-do PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(littlefs-do PRIVATE std::filesystem)
