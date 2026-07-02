# AGENTS.md — AI Coding Agent Reference

## Project Summary

**elwinch** is Arduino firmware that translates two control sources (RC receiver and potentiometer) into signals for a Kelly KLS96601 ESC driving a QS 16" 8kW electric winch motor. The entire project lives in a single file: `elwinch.ino`.

## Architecture

```
loop()
 ├── Read inputs (3-way switch + calib button)
 ├── CalibRemote()  — state machine for remote throttle endpoint calibration
 ├── RunMotor()     — decide ESC output & brake relay per active mode
 └── PrintSerial()  — debug output over serial
```

**Three operating modes**, selected by a 3-way switch:

| Switch | Mode | Input | Output mapping |
|---|---|---|---|
| Top | Remote | RC PWM pulse (D7) | `map(pulse, calibLo, calibHi, 0, 92)` |
| Middle | Brake | — | Output forced to 0, brake engaged |
| Bottom | Local | Potentiometer ADC (A2) | `map(0–1024, 0, 92)` |

## Calibration State Machine

Triggered by holding the calib button while in remote mode. Uses **intentional switch-case fall-through** (no `break` statements) to allow multiple states to execute in a single loop iteration when timing conditions are met.

```
NOMINAL (0) ──button+remote──▶ SIGNAL_CALIB_MIN (10)  ← lamp on 1s, prompt user
                                  │
                                  ▼
                             CALIBRATE_MIN (20)        ← sample 100 pulses → average
                                  │
                                  ▼
                             CALIB_MIN_DONE (30)       ← blink 3× fast
                                  │
                                  ▼
                             SIGNAL_CALIB_MAX (40)     ← lamp on 1s, prompt user
                                  │
                                  ▼
                             CALIBRATE_MAX (50)        ← sample 100 pulses → average
                                  │
                                  ▼
                             CALIB_MAX_DONE (60)       ← blink 3× fast
                                  │
                                  ▼
                             CALIBRATION_DONE (99)     ← wait for button release
```

- Timing-gated states (blinks, prompts) take multiple loop iterations.
- Compute states (sampling) run to completion in one pass (blocking ~3s with `pulseIn` + `delay(30)`).
- Exiting remote mode at any point forces the state back to `NOMINAL`.

## Pin Layout

Target board: **Arduino Nano Every** (`arduino:megaavr:nona4809`)

| Pin | Direction | Label | Purpose |
|---|---|---|---|
| A2 | Analog in | `aiLocalPot` | Local potentiometer (manual throttle) |
| D2 | Digital in | `diRemoteOperationInput` | Remote mode switch (3-way, top) |
| D3 | Digital in | `diLocalOperationInput` | Local mode switch (3-way, bottom) |
| D4 | Digital in | `diCalibButton` | Calibration pushbutton |
| D5 | Digital out | `doBreakRelayOff` | Brake relay (HIGH = brake released) |
| D6 | Digital out | `doCalibLampOn` | Calibration status lamp |
| D7 | Digital in | `diRemoteInputThrottle` | RC receiver PWM pulse |
| D9 | Analog out | `aoMotorController` | PWM output to ESC |

All digital inputs use `INPUT_PULLUP` — switches connect to GND, so **LOW = active**.

## Naming Conventions

- `ai*` — analog input pin
- `ao*` — analog output pin
- `di*` — digital input pin
- `do*` — digital output pin
- Operations use `and`/`not` keywords (not `&&`/`!`)

## Build System

Uses **arduino-cli** via Makefile:

```bash
make deps          # Install board core (arduino:megaavr)
make upload        # Compile & upload (default target)
make upload PORT=/dev/ttyUSB0  # Override serial port
make compile       # Compile only
make monitor       # Open serial monitor (9600 baud)
make list-boards   # List connected boards
```

Default config (override on command line):
- `BOARD` = `arduino:megaavr:nona4809`
- `PORT` = `/dev/ttyACM0`

## Hardware Components

- **Motor**: QS 16" rated for 8kW
- **ESC**: Kelly Controller KLS96601
- **Control board**: potentiometer, status LED, 3-way mode switch, calibration button
- **Arduino**: Nano Every — translates control inputs into ESC PWM (0–5V equivalent)
