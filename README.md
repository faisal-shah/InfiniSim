# [InfiniSim](https://github.com/faisal-shah/InfiniSim)

[![Build InfiniSim LVGL Simulator](https://github.com/faisal-shah/InfiniSim/actions/workflows/lv_sim.yml/badge.svg)](https://github.com/faisal-shah/InfiniSim/actions/workflows/lv_sim.yml)

Simulator for [InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime) project.

Experience the `InfiniTime` user interface directly on your PC, to shorten the time until you get your hands on a real [PineTime smartwatch](https://www.pine64.org/pinetime/).
Or use it to develop new Watchfaces, new Screens, or quickly iterate on the user interface.

For a history on how this simulator started and the challenges on its way visit the [original PR](https://github.com/InfiniTimeOrg/InfiniTime/pull/743).

## Get the Sources

Clone InfiniSim, initialize its display-driver submodule, and check out the
family InfiniTime tree:

```sh
git clone https://github.com/faisal-shah/InfiniSim.git
cd InfiniSim
git submodule update --init lv_drivers
git clone --recursive --branch family-features \
  https://github.com/faisal-shah/InfiniTime.git ../InfiniTime
```

## Build dependencies

- CMake 3.12 or newer
- SDL2 (provides the simulator window, handles mouse and keyboard input)
- Compiler (g++ or clang++)
- [lv_font_conv](https://github.com/lvgl/lv_font_conv#install-the-script) (for `font.c` generation since [InfiniTime#1097](https://github.com/InfiniTimeOrg/InfiniTime/pull/1097))
  - Note: requires Node.js v14.0.0 or later
- [Pillow](https://python-pillow.org/) (for `resource.zip` generation when `BUILD_RESOURCES=ON`, which is the default)
- optional: `libpng`, see `-DWITH_PNG=ON` cmake setting below for more info

On Ubuntu/Debian install the following packages:

```sh
sudo apt install -y cmake libsdl2-dev g++ npm libpng-dev
```

On Arch Linux the following packages are needed:

```sh
sudo pacman -S cmake sdl2 gcc npm libpng
```

On Fedora the following packages are needed:

```sh
sudo dnf install cmake SDL2-devel g++ npm patch perl libpng-devel
```

On OpenSUSE (Tumbleweed) the following packages are needed:

```sh
sudo zypper install cmake libSDL2-devel gcc-c++ gcc npm libpng16-devel patch
```

Then install the `lv_font_conv` executable to the InfiniSim source directory (will be installed at `node_modules/.bin/lv_font_conv`)

```sh
npm install lv_font_conv@1.5.2
```

When you want to create a `resource.zip` file then install the `pillow` Python library to the InfiniSim source directory (will be installed in `.venv/`)

```sh
python3 -m venv .venv
source .venv/bin/activate
pip install wheel Pillow
```

Optionally, depending on your distro, it may also serve the Pip package as official native installation packages:

On Ubuntu/Debian 

```sh
sudo apt install python3-pil
```

On OpenSUSE (Tumbleweed) 

```sh
sudo zypper install python311-Pillow
```

## Configure and Build

In the most basic configuration tell cmake to configure the project and build it with the following two commands:

```sh
cmake -S . -B build -DInfiniTime_DIR=../InfiniTime
cmake --build build -j4
```

The following configuration settings can be added to the first `cmake -S . -B build` call

- `-DInfiniTime_DIR=../InfiniTime`: path to the
  `faisal-shah/InfiniTime` `family-features` checkout. Its recursive submodules
  must be initialized. CMake rejects trees without the generated companion
  manifest and portable BLE policy sources.
- `-DMONITOR_ZOOM=1`: scale simulator window by this factor
- `-DBUILD_RESOURCES=ON`: enable/disable `resource.zip` creation, will be created in the `<build-dir>/resources` folder
- `-DWITH_PNG=ON`: enable/disable the screenshot to `PNG` support.
  Per default InfiniSim tries to use `libpng` to create screenshots in PNG format.
  This requires `libpng` development libraries as build and runtime dependency.
  Can be disabled with cmake config setting `-DWITH_PNG=OFF`.
- `-DENABLE_USERAPPS`: ordered list of user applications to build into InfiniTime.
  Values must be fields from the enumeration `Pinetime::Applications::Apps` and must be separated by a comma.
  Ex: `-DENABLE_USERAPPS="Apps::Timer, Apps::Alarm"`.
  The default list of user applications will be selected if this variable is not set.
- `-DENABLE_BLE_TEST_CONTROL=ON`: build the virtual BLE test-control endpoint.
  The endpoint is available only when `--ble-control PORT` is passed at runtime
  and binds to `127.0.0.1`.

### Build with Docker

You can also build the simulator using Docker.
This is useful if you don't want to install all the dependencies on your system.
First build the Docker image:
```sh
docker build -t infinisim-build .devcontainer
```

Afterwards you can build the simulator with:
```sh
docker run --rm -it -v ${PWD}:/sources --user $(id -u):$(id -g) infinisim-build
```

Note: when using rootless `podman` instead of `docker` the `--user` part can be left out.
The command to build the simulator using `podman` is:
```sh
podman run --rm -it -v ${PWD}:/sources infinisim-build
```

Mount the family InfiniTime checkout over `/sources/InfiniTime`:
```sh
docker run --rm -it -v ${PWD}:/sources -v ${PWD}/../InfiniTime:/sources/InfiniTime --user $(id -u):$(id -g) infinisim-build
```

Other CMake generation and build arguments can be passed to the `GENERATE_ARGS` and `BUILD_ARGS` variables:
```sh
docker run --rm -it -v ${PWD}:/sources -e GENERATE_ARGS=-DENABLE_USERAPPS="Apps::Timer,Apps::Alarm" -e BUILD_ARGS=-j16 --user $(id -u):$(id -g)  infinisim-build
```


## Run Simulator

When the build was successful the simulator binary can be started with

```sh
./build/infinisim
```

![Running Simulator](https://user-images.githubusercontent.com/9076163/151057090-66fa6b10-eb4f-4b62-88e6-f9f307a57e40.gif)

To hide the second simulator-status-window start the binary with the `--hide-status` option

```sh
./build/infinisim --hide-status
```

- Left mouse button: simulates your finger, just click to tap, click and drag to swipe
- Right mouse button: simulates the hardware button (for example turn the screen off or on again)

Using the keyboard the following events can be triggered:

- `r` ... enable ringing
- `R` ... disable ringing
- `m` ... let motor run for 100 ms
- `M` ... let motor run for 255 ms
- `n` ... send notification
- `N` ... clear new notification flag
- `b` ... connect Bluetooth
- `B` ... disconnect Bluetooth
- `v` ... increase battery voltage and percentage
- `V` ... decrease battery voltage and percentage
- `c` ... charging,
- `C` ... not charging
- `l` ... increase brightness level
- `L` ... lower brightness level
- `p` ... enable print lvgl memory usage to terminal
- `P` ... disable print memory usage
- `s` ... increase step count by 500 steps
- `S` ... decrease step count by 500 steps
- `h` ... set heartrate running, and on further presses increase by 10 bpm
- `H` ... stop heartrate
- `i` ... take screenshot
- `I` ... start/stop Gif screen capture
- `w` ... generate weather data
- `W` ... clear weather data

Additionally using the arrow keys the respective swipe gesture can be triggered.
For example pressing the UP key triggers a `SwipeUp` gesture.

## Portable BLE policy simulation

InfiniSim compiles these files directly from `InfiniTime_DIR`:

- `BleRadioStateMachine`
- `BondRegistry`
- `BondStorePolicy`
- `BondStoreCodec`
- `BondPersistenceCoordinator`
- `CompanionManagementService`
- `CompanionManagementStatus` encoder

The simulator supplies deterministic virtual command, peer, store, filesystem,
time, and event ports around that code. It does not link full NimBLE and does
not simulate RF, SMP key exchange, controller scheduling, current, or power.
Security records are opaque deterministic test fixtures.
`sim/generated/CompanionProtocolMetadata.h` is generated from
`protocol/companion.json`; CMake verifies its recorded manifest digest before
compiling.

| Behavior | Fidelity |
|---|---|
| Radio desired/actual transitions, retries, fast/slow policy | Real portable InfiniTime state machine |
| Five retained peers, sixth-peer LRU, repeat replacement | Real portable InfiniTime bond policy |
| Bond encoding, CRC validation, restore, dirty/write scheduling | Real portable InfiniTime codec/coordinator |
| GATT bytes and firmware service callbacks | Real firmware service code over a TCP bridge |
| Authentication decisions | Generated characteristic metadata plus injected virtual link security |
| Advertising, connection, pairing | Virtual policy events only; no RF or SMP |
| Flash and wake-lock reporting | Event/write/state proxies, not electrical measurements |

Fresh and pre-marker stores follow the 2.0.2 boot contract: the simulator
restores an empty RAM registry, reports `initializing_empty`, keeps virtual
advertising off, and releases fast advertising only after the atomic write
succeeds. Injected write failures remain visible and retry with the real
coordinator backoff.

The GATT bridge defaults each new transport connection to an explicitly
prebonded, authenticated virtual test peer for companion-app compatibility.
Test control can select unauthenticated or bonded-only peers. Protected
characteristics return ATT error `0x05` unless the injected security state is
authenticated; companion status (bridge ID 33) remains public. Bridge ID 34
verifies only authenticated virtual links.

Only one GATT bridge client is active. A second client receives the three-byte
busy response `fd 00 00` and is closed without replacing the incumbent.
Replacement requires an explicit test-control command.

### Loopback BLE test control

Start the simulator with separate data and control ports:

```sh
./build/infinisim --gatt-bridge 8080 --ble-control 8081
```

The control socket is newline-delimited UTF-8, bound only to
`127.0.0.1`. Commands and tokens are case-sensitive. Each command returns one
line beginning with `OK` or `ERR`.

```text
QUERY
NEXT_PEER <type 0..3> <12 hex address> <UNAUTHENTICATED|BONDED|AUTHENTICATED> [REPLACE]
CONNECT
DISCONNECT
FORCE_CONNECT
FORCE_DISCONNECT
GAP_RESULT <START|STOP|TERMINATE> <integer result> <SUCCESS|ALREADY_INACTIVE|ADVERTISING_ACTIVE|FAILED>
ADVANCE <milliseconds>
DRAIN
STORE_FAILURE <NONE|READ|WRITE>
POWER_CUT <NONE|BEFORE_REPLACE|AFTER_PARTIAL_STAGED_WRITE|AFTER_STAGED_WRITE>
CCCD <u16 handle> <u16 flags>
REBOOT
RESET
```

`QUERY` reports virtual radio/link/security/bond state, coordinator boot and
write state, GAP and host-policy event counts, successful flash writes/bytes,
and the persistence wake-lock-duration proxy. `REBOOT` preserves and restores
`infinisim-ble-bonds.bin`; `RESET` removes it. The named power cuts operate only
at deterministic staged-write/replacement boundaries and never tear the live
file. Companion status and verify call the
selected InfiniTime tree's real management service and portable status encoder.

## Littlefs-do helper

To help working with the SPI-raw file the tool `littlefs-do` is provided (in the build directory).
The SPI-raw file emulates the persistent 4MB storage available over the SPI bus on the PineTime.

```sh
$ ./littlefs-do --help
Usage: ./littlefs-do <command> [options]
Commands:
  -h, --help           show this help message for the selected command and exit
  -v, --verbose        print status messages to the console
  stat                 show information of specified file or directory
  ls                   list available files in 'spiNorFlash.raw' file
  mkdir                create directory
  rmdir                remove directory
  rm                   remove directory or file
  cp                   copy files into or out of flash file
  settings             list settings from 'settings.h'
  res                  resource.zip handling
```

### Resource loading

To load resource zip files into the SPI raw file for the simulator to use the `res load` command can be used.

```sh
$ ./littlefs-do res --help
Usage: ./littlefs-do res <action> [options]
actions:
  load res.zip         load zip file into SPI memory
Options:
  -h, --help           show this help message for the selected command and exit
```

## Licenses

This project is released under the GNU General Public License version 3 or, at your option, any later version.
The same license as [InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime).

The simulator is based on [lv_sim_eclipse_sdl](https://github.com/lvgl/lv_sim_eclipse_sdl) project under the MIT license.

## Agent handoff

Agents continuing the family BLE simulation work must read
`.memory/context.md`, `.memory/progress.md`, and `.memory/lessons.md`. The
simulator integration is complete; remaining acceptance is physical and must
not be replaced with fake RF, SMP, or current features.
