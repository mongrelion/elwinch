# elwinch
Source code for the Arduino that drives the elwinch.

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
| `BOARD` | `arduino:avr:uno` | `arduino:avr:nano` |
| `PORT` | `/dev/ttyACM0` | `/dev/ttyUSB0` |

Examples:

```bash
make upload PORT=/dev/ttyUSB0
```
