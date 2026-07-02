# elwinch

Arduino firmware for an electric winch controller. Translates signals from an RC receiver and a manual potentiometer into PWM output for a Kelly KLS96601 ESC driving a QS 16" 8kW motor. Supports three modes via a 3-way switch: remote (RC throttle), local (potentiometer), and brake.

## Background

### Components
- Motor: QS 16" rated for 8kW
- ESC: Kelly Controller KLS96601
- Control board: potentiometer, status LED, 3-way mode switch (remote / brake / local), calibration button
- Arduino: Nano Every — converts potentiometer and RC receiver signals to the PWM voltage expected by the ESC

## Build & Upload

This project uses a Makefile backed by [arduino-cli](https://arduino.github.io/arduino-cli/) for compiling and uploading sketches.

### Setup

Install the board core:

```bash
make deps
```

### Common commands

| Command | Description |
|---|---|
| `make upload` | Compile and upload to the board |
| `make compile` | Compile only |
| `make monitor` | Open serial monitor |
| `make list-boards` | List connected and installed boards |

### Configuration

Override any of these variables on the command line:

| Variable | Default | Example |
|---|---|---|
| `BOARD` | `arduino:megaavr:nona4809` | `arduino:avr:uno` |
| `PORT` | `/dev/ttyACM0` | `/dev/ttyUSB0` |

Examples:

```bash
make upload PORT=/dev/ttyUSB0
```

## TODO

Known issues and improvements to address:

1. **`PrintSerial;` missing function call** — In `loop()`, `PrintSerial;` lacks parentheses and is a no-op in C/C++ (evaluates the function address, discards it). Should be `PrintSerial();` if serial output is intended each loop iteration.

2. **Off-by-one in `CALIBRATE_MAX` sampling** — The `for` loop starts at `i = 2` instead of `i = 1`, taking 99 readings instead of 100 for the max-throttle endpoint. Practically negligible but worth fixing.

3. **Potentiometer map range** — Local mode maps `0–1024` instead of `0–1023`. The ATmega4809 ADC is 10-bit (0–1023), so the upper end is slightly compressed.

4. **No EEPROM persistence for calibration** — `remoteLowValue` and `remoteHighValue` are stored in RAM only. They reset to hardcoded defaults (1582/2014) on power cycle. Calibrated endpoints should be persisted in EEPROM.

5. **Mixed local/global access pattern** — `CalibRemote()` receives mode flags by value (as parameters) but directly mutates the global `remoteCalibModeActive`. The parameter names (`remoteModeActive`, `calibButtonActive`) also shadow similarly-named globals (`remoteModeSelected`, `calibButtonActive`). Consider passing by reference or refactoring state ownership.

6. **Blocking loops during calibration** — `CALIBRATE_MIN` and `CALIBRATE_MAX` block for ~3 seconds (`100 × pulseIn + delay(30)`). The Arduino is unresponsive to the mode switch during this window. Consider a non-blocking sampling approach.
