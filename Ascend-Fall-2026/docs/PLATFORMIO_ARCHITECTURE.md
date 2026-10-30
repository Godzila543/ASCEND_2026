# ASCEND PlatformIO Architecture Guide

## 1. Scope and current state

This repository is embedded firmware for an Adafruit Feather RP2350 HSTX running
Arduino through PlatformIO. It is not a backend, frontend, or multi-service
platform. The firmware is intended to initialize a sensor payload, read
measurements, write CSV data to removable storage, and expose status through a
NeoPixel ring.

The checked-in implementation is currently a scaffold rather than an active
data-collection application:

- [`src/main.cpp`](../src/main.cpp) contains empty `setup()` and `loop()` bodies;
  the `CNS` object and all orchestration calls are commented out.
- [`src/CNS.hpp`](../src/CNS.hpp) registers exactly one sensor,
  `TripleSpectral`; the other sensor registrations are commented out.
- The sensor, SD, multiplexer, and NeoPixel implementations exist, but the
  runtime does not currently invoke them.
- `pio run` could not be executed during this inspection because the PlatformIO
  CLI is not installed in the current environment. No build result should be
  inferred from this document.

## 2. Repository structure

```text
Ascend-Fall-2026/
├── platformio.ini              PlatformIO target, board, framework, dependencies
├── TODO.md                     Hardware and feature checklist
├── src/                        Application firmware and sensor adapters
│   ├── main.cpp                Arduino entry point
│   ├── CNS.cpp/.hpp            Central orchestration/controller
│   ├── Sensor.hpp              Polymorphic sensor contract
│   ├── *Sensor*.hpp            Individual sensor adapters
│   ├── BME680.hpp, BMP390.hpp  Environmental sensor adapters
│   ├── BNO085.hpp, IMU.hpp     IMU adapters
│   ├── TripleSpectral.hpp      Active 18-channel spectral adapter
│   ├── SpectralSensor.hpp      Visible spectral adapter
│   ├── IRSensor.hpp            IR spectral adapter
│   ├── UVSensor.hpp            UV adapter
│   ├── GravityOzone.hpp         Ozone adapter
│   ├── Magnetometer.hpp         AS5600 adapter
│   ├── INA260.hpp               Current/voltage/power adapter
│   ├── TempSensor.hpp           AHTX0 temperature/humidity adapter
│   ├── AirQualitySensor.hpp     SGP40 VOC adapter
│   ├── NeopixelRing.hpp         Status LED abstraction
│   ├── SD.cpp/.hpp              CSV logging to SPI SD
│   ├── FileSystem.cpp/.hpp      FatFS USB mass-storage callbacks
│   └── main.test                NeoPixel example/test sketch
├── lib/src/                    Local low-level/support code
│   ├── DEV_Config.*             Generic Arduino/I2C compatibility layer
│   ├── SGP40.*                  SGP40 command and VOC wrapper
│   ├── DFRobot_OzoneSensor.*   Local ozone driver
│   ├── sensirion_*.h/.c        Sensirion VOC algorithm/configuration
│   └── Debug.h                  Debug support include
├── include/README              PlatformIO include directory placeholder
├── lib/README                   PlatformIO library directory placeholder
└── test/README                  PlatformIO test directory placeholder
```

The application source contains 24 files and the local library source contains
10 files. Most sensor implementations are header-only classes. This keeps
registration simple, but couples hardware-library includes, sensor state, and
the public adapter contract in one translation unit.

## 3. Build and deployment configuration

[`platformio.ini`](../platformio.ini) defines one environment:

| Setting | Value | Effect |
|---|---|---|
| Environment | `feather_rp2350` | The only build target |
| Platform | `https://github.com/maxgerhardt/platform-raspberrypi.git` | Raspberry Pi RP2350 PlatformIO platform |
| Board | `adafruit_feather_rp2350_hstx` | Hardware pins, upload protocol, and board metadata |
| Framework | `arduino` | Arduino lifecycle and APIs |
| Core | `earlephilhower` | Raspberry Pi Arduino core selection |
| Filesystem | `6m` | Board filesystem allocation |
| Monitor | `115200` | Serial monitor baud rate |
| Upload port | `COM4` | Machine-specific upload port |
| Upload speed | `115200` | Upload baud rate |

The dependency list selects libraries from SparkFun, Adafruit, Bosch, DFRobot
local code, and Makuna. The repository does not pin a lockfile, so dependency
resolution is version-range based (`^`). The machine-specific `COM4` setting
should not be treated as portable project configuration.

## 4. Runtime architecture

### 4.1 Layers

The implementation has four practical layers:

1. **Arduino runtime layer** — `setup()` and `loop()` in `main.cpp`.
2. **Payload orchestration layer** — `CNS` owns sensor instances, the I2C
   multiplexer, SD logger, and NeoPixel status device.
3. **Hardware adapter layer** — classes implementing `Sensor`, each translating
   one vendor library into a common read/data/descriptor shape.
4. **Infrastructure and driver layer** — `uSD`, FatFS USB callbacks, local
   I2C wrappers, vendor libraries, and the Sensirion VOC algorithm.

There is no separate configuration, scheduling, telemetry, domain-model, or
error-reporting layer. Timing is cooperative and blocking: calls such as
`delay()`, sensor conversion waits, SD reopen operations, and LED updates all
run on the Arduino loop thread.

### 4.2 Boot and sampling flow

The intended flow, currently commented out in `main.cpp`, is:

1. Start serial, Wire, and SPI.
2. Initialize the NeoPixel ring and show `BOOT`.
3. Initialize the SD card and create a sequential `<n>.csv` file.
4. Initialize the I2C multiplexer and disable all eight ports.
5. For each registered sensor, select its `muxIndex` and retry `init()` up to
   three times.
6. Emit operational/failed sensor lists and CSV headers.
7. On each loop, select each sensor's mux port, call `readData()`, serialize
   `millis()/1000.0` and each sensor value, then refresh the SD file.

The actual checked-in boot flow does none of these steps because the calls are
commented out.

### 4.3 Controller (`CNS`)

[`CNS.hpp`](../src/CNS.hpp) is the composition root. Its fixed array currently
contains one heap-allocated `TripleSpectral`. It also owns:

- `uSD sd` for SPI SD logging;
- `QWIICMUX mux` for selecting I2C channels;
- `NeopixelRing neopixelRing` for status;
- a fixed LED pin value (`13`);
- an unused `sensorsError` counter.

[`CNS.cpp`](../src/CNS.cpp) implements lifecycle operations and the read/log
cycle. Sensor selection is based on the public `muxIndex` field inherited from
`Sensor`; `-1` means direct I2C rather than a mux port. The controller does not
validate sensor array indexes, does not own/free the allocated sensor objects,
and does not propagate most hardware failures to callers.

### 4.4 Sensor contract

[`Sensor.hpp`](../src/Sensor.hpp) defines the common adapter interface:

```cpp
virtual int init() = 0;
virtual bool isOperational();
virtual void readData() = 0;
virtual double* getData() = 0;
virtual String getName();
virtual String* getDescriptors();
virtual int getDataCount();
```

Each adapter stores a fixed `double` array, a parallel descriptor `String`
array, and an `operational` flag. `CNS` assumes the descriptor and data arrays
have exactly `getDataCount()` elements and logs them by position. This is a
positional, schema-by-convention interface: there is no type, unit, validity,
timestamp, quality flag, or error value associated with an individual sample.

The active `TripleSpectral` adapter reads 18 calibrated AS7265X channels after
configuring gain, measurement mode, integration cycles, bulbs, and indicator
current. Other implemented adapters include:

| Adapter | Hardware/library | Values |
|---|---|---:|
| `BNO085` | Adafruit BNO08x | 16 |
| `IMU` | SparkFun ICM-20948 | 9 |
| `BME680` | Adafruit BME680 | 5 |
| `BMP390` | Adafruit BMP3XX | 3 |
| `INA260` | Adafruit INA260 | 3 |
| `TempSensor` | Adafruit AHTX0 | 2 |
| `UVSensor` | SparkFun AS7331 | 3 |
| `SpectralSensor` | SparkFun AS726X | 6 |
| `IRSensor` | SparkFun AS726X | 6 |
| `TripleSpectral` | SparkFun AS7265X | 18 |
| `GravityOzone` | Local DFRobot driver | 1 |
| `Magnetometer` | AS5600 | 2 |
| `AirQualitySensor` | Local SGP40 wrapper | 1 |

Several adapters use identical names for unrelated concepts (`IMU` and
`BNO085` both report `"IMU"`), and some descriptors are placeholders or
ambiguous (`"A"`, `"B"`, `"C"`, `"YG"`). The CSV schema is therefore not
self-describing enough for reliable downstream analysis.

## 5. Storage and host access

### 5.1 SPI SD logging

[`uSD`](../src/SD.hpp) uses Arduino `SD` over SPI chip select `6`. `setup()`
searches for the next available numeric filename and opens it. `write_data()`
prints to serial in debug mode or appends to the open file. `loop()` closes and
reopens the file to flush/refresh access, and falls back to `setup()` when the
open fails.

`CNS::createCSVHeaders()` writes a timestamp column followed by
`SensorName(Descriptor)` columns. `CNS::printSensorData()` writes the values but
uses `", "` after data values while headers use `","`, so the output is
loosely CSV rather than a consistently specified format.

### 5.2 FatFS USB mass storage

[`FileSystem.cpp`](../src/FileSystem.cpp) is a separate FatFS path. It exposes
the filesystem to a host computer through `FatFSUSB`, using the volatile
`pcHasDrive` flag to stop MCU-side writes while the host owns the drive.
`driveReady()` calls `FatFS.end()` and `onUnplug()` remounts with `FatFS.begin()`.
`loopUSB()` writes a fixed sample line to `/log.csv`.

This path does not share the `uSD`/Arduino `SD` object or the CNS logger. Unless
the board setup intentionally provides two distinct storage stacks, the
repository currently has two competing storage abstractions and no integrated
USB handoff in the main loop.

## 6. Local drivers and important implementation details

- `DEV_Config` wraps Arduino `Wire`, but stores one global I2C address and uses
  global bus state. It is primarily consumed by the SGP40 code.
- `SGP40.cpp` performs feature-set and self-test commands, then computes a VOC
  index using the bundled Sensirion algorithm. The wrapper uses fixed command
  buffers and 500 ms delays.
- `DFRobot_OzoneSensor` implements passive/automatic register operations and a
  moving average over up to 100 samples.
- `NeopixelRing` drives 12 GRB pixels on pin 10. Status maps to solid colors:
  off, green (`ON`), lime (`BOOT`), red (`ERROR`), and yellow (`SD_ERROR`).
  `update()` writes verbose serial diagnostics and `show()` introduces a
  30 ms delay.
- `main.test` is a standalone NeoPixel cycling example, not a host-side
  automated test. `test/README` is only a placeholder.

## 7. Design decisions and trade-offs

### Deliberate or apparent decisions

- **Polymorphic sensors:** one `Sensor` contract lets the controller loop over
  different hardware without knowing each vendor API.
- **Fixed-size embedded storage:** static arrays avoid dynamic measurement
  containers and make memory use predictable.
- **I2C multiplexing:** each sensor carries a mux port number so identical
  address devices can coexist.
- **CSV output:** human-readable removable-media logs are prioritized over a
  binary or network telemetry protocol.
- **Status LEDs:** boot, storage, and operational state are surfaced without
  requiring a serial console.
- **Local driver copies:** SGP40 and ozone code can be built with the project
  without relying entirely on registry libraries.

### Consequences

- Sensor registration, ordering, schema, and ownership are all hard-coded in
  `CNS.hpp`.
- A sensor can only report a flat list of `double` values. Units, sample age,
  errors, and calibration state are implicit in string descriptors.
- Blocking I2C, SD, and LED operations can make the sample period unpredictable.
- Vendor-library failures are mostly reduced to `-1` during initialization;
  read failures are not represented.
- The firmware has no explicit state machine for boot, degraded operation,
  logging, USB ownership, or recovery.

## 8. Quality and correctness findings

These are source-level findings, not claims from a successful build:

1. **The application is inert by default.** `main.cpp` does not instantiate
   `CNS` or call any controller method.
2. **Registration is manually duplicated.** The array length `[1]`, allocation,
   include list, and commented alternatives must be kept synchronized.
3. **Initialization failures are not tracked correctly.** `sensorsError` is
   never incremented, so the “all sensors operational” status can be reported
   even after retries fail.
4. **Retry behavior is incomplete.** A failed sensor remains in the collection
   and there is no explicit degraded-state record or per-sensor failure reason.
5. **Bounds and ownership are unsafe.** `getSensor(int)` has no bounds check,
   and `new TripleSpectral()` is never released. This is less important on a
   process-lifetime firmware but still obscures ownership.
6. **The sensor interface is weakly typed.** Raw pointers, parallel arrays,
   mutable Arduino `String`, and positional counts make mismatches easy.
7. **Several adapters are incomplete.** `AirQualitySensor::init()` does not
   set `operational` or return on all paths; `BME680` and `BMP390` ignore the
   result of `performReading()`; and `BNO085` ignores event-read success.
8. **Low-level I2C reads need defensive checks.** The local wrappers do not
   consistently check transmission/request lengths; `I2C_Read_Nbyte` uses a
   variable-length stack array and assumes two returned bytes.
9. **Data contracts are inconsistent.** Headers and rows use different spacing,
   units are sometimes absent or incorrect, and some values are named only by
   letters.
10. **Storage ownership is ambiguous.** `SD` logging and FatFS USB logging use
    separate APIs, filenames, and lifecycle flags without a shared arbitration
    boundary.
11. **Timing is unbounded.** Sensor conversion delays, SD close/reopen, serial
    flushing, and NeoPixel delays occur inside the sampling path.
12. **Configuration is scattered.** Pin numbers, mux addresses, sample behavior,
    status colors, and sensor setup values are literals in individual classes.

## 9. Recommended improvement roadmap

### Small revisions: low-risk quality baseline

1. Make `main.cpp` intentionally functional or explicitly label the firmware
   as a bring-up stub. Create `CNS cns;`, initialize serial/buses, and call the
   lifecycle methods only after deciding the intended boot sequence.
2. Replace magic values with named `constexpr` configuration values for SD CS,
   LED pins, NeoPixel count, mux port count, retry count, and timing.
3. Add `const` correctness, `override`, virtual destructors, and include guards
   consistently. Change sensor getters that do not mutate state to `const` where
   the vendor APIs permit it.
4. Make `getSensor()` bounds-safe and return `nullptr` or a documented error.
5. Fix initialization status propagation: set `operational` in every adapter,
   return a defined status on every path, and increment/report per-sensor
   failures.
6. Check every vendor read result and preserve the last-good sample with a
   validity/error status rather than silently logging stale or zero values.
7. Standardize descriptors, units, delimiters, and numeric formatting. Define a
   CSV schema document or constants next to the logger.
8. Remove dead includes, duplicated includes, misleading comments, and example
   code from production paths. Move `main.test` into a clearly named example
   or PlatformIO test fixture.
9. Add a top-level README describing hardware addresses, pins, boot sequence,
   supported sensors, and the required PlatformIO installation.

### Medium changes: make behavior testable and reliable

1. Introduce a `SensorReading`/`Measurement` record containing a stable key,
   display name, unit, value, timestamp, and validity/error code. Replace
   parallel raw arrays with a bounded collection or compile-time measurement
   table.
2. Replace raw `Sensor*` ownership with a fixed-capacity registry of references
   or smart pointers appropriate for the embedded toolchain. Centralize sensor
   metadata and registration.
3. Extract `I2CBus`/`MuxChannel`, `Logger`, `StatusIndicator`, and `Clock`
   interfaces. Keep hardware adapters dependent on those boundaries so host
   tests can use fakes.
4. Add a non-blocking scheduler with per-sensor sampling periods and deadlines.
   A slow UV, ozone, or spectral measurement should not stall every other
   sensor.
5. Create one storage manager that arbitrates SD logging and USB mass-storage
   ownership. Stop logging, flush, unmount, and resume through explicit states.
6. Add structured boot/degraded/recovery states and a per-sensor health table.
   Reflect the aggregate state in the NeoPixel ring and serial diagnostics.
7. Add PlatformIO unit tests for CSV schema generation, sensor registry
   behavior, retry/state transitions, I2C error handling, and USB ownership.
   Use dependency injection or compile-time fakes for vendor libraries.
8. Add CI that installs PlatformIO, performs a clean build, runs tests, and
   checks formatting/static analysis. Keep upload ports out of shared config.

### Large changes: complete architecture restructure

1. **Use a layered firmware architecture.**

   ```text
   Hardware drivers
       -> device adapters
       -> measurement services
       -> scheduler/health state machine
       -> sinks (CSV, USB, telemetry)
       -> board composition root
   ```

   The composition root would be the only place that selects concrete hardware.
   Application services would not include vendor headers.

2. **Define an explicit measurement model.** Give every channel a stable
   machine-readable ID, unit, calibration metadata, source sensor, timestamp,
   validity, and error state. Generate CSV headers and rows from the same
   schema, preventing positional drift.

3. **Move from a monolithic CNS controller to bounded components.** Separate
   sensor lifecycle, mux arbitration, scheduling, health/retry policy, storage,
   status indication, and USB ownership. `CNS` can remain as a thin board-level
   facade or disappear into a composition module.

4. **Adopt an event/state-machine model.** Represent events such as
   `BootRequested`, `SensorReady`, `SensorFailed`, `SampleReady`, `StorageLost`,
   `UsbMounted`, and `UsbReleased`. This makes degraded operation and recovery
   deterministic instead of relying on nested loops and delays.

5. **Unify storage.** Choose one filesystem stack for the board, define a
   transactional append/flush API, and make USB export a controlled mode. If
   both SPI SD and FatFS are required, model them as separate sinks behind one
   ownership coordinator rather than letting each path open files independently.

6. **Make hardware profiles data-driven.** Store sensor enablement, I2C address,
   mux channel, sample interval, gain, integration time, and logging policy in
   a board profile. This permits hardware variants without editing the
   controller's source.

7. **Introduce verification at the firmware boundary.** Build a host-side
   simulator for orchestration and logging, hardware-in-the-loop smoke tests
   for each sensor adapter, and fault-injection tests for I2C, SD, USB takeover,
   and sensor timeouts.

This restructure is justified only if the payload will support multiple hardware
configurations, reliable long-duration operation, or telemetry beyond the
current single-board prototype. For a one-off bring-up, the small and medium
steps provide most of the value with much lower risk.

## 10. Suggested implementation order

1. Establish a reproducible PlatformIO build and CI job.
2. Activate a minimal boot path with one sensor and one logger.
3. Fix initialization/read error propagation and define the CSV schema.
4. Add tests around the registry, logger, and state transitions.
5. Introduce scheduling and explicit storage ownership.
6. Add sensors incrementally, each with a hardware test and documented channel
   schema.
7. Decide whether the larger layered/state-machine restructure is warranted by
   measured sampling, storage, and hardware-variant requirements.
