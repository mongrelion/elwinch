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

1. ~~**`PrintSerial;` missing function call**~~ — Fixed: changed to `PrintSerial();`.
2. ~~**Off-by-one in `CALIBRATE_MAX` sampling**~~ — Fixed: loop now starts at `i = 1`.
3. ~~**Potentiometer map range**~~ — Fixed: local mode now maps `0–1023` (10-bit ADC).
4. ~~**No EEPROM persistence for calibration**~~ — Fixed: calibration endpoints are now persisted to EEPROM with a magic-byte validity check, loaded on boot.
5. ~~**Mixed local/global access pattern**~~ — Fixed: `CalibRemote()` and `RunMotor()` now read global state flags directly instead of taking shadowing parameters.
6. **Blocking loops during calibration** — This is intentional. The ~3-second blocking loops during `CALIBRATE_MIN` and `CALIBRATE_MAX` ensure the calibration sampling is not interrupted by mode-switch changes, keeping the calibration sequence deterministic.
