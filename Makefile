PORT  ?= /dev/ttyACM0
BOARD ?= arduino:megaavr:nona4809

deps:
	@arduino-cli core update-index
	@arduino-cli core install arduino:megaavr

default: upload

compile:
	@arduino-cli compile --fqbn $(BOARD) $(PWD)

upload: compile
	@arduino-cli upload $(PWD) \
		--verify \
		--port $(PORT) \
		--fqbn $(BOARD)

monitor:
	@arduino-cli monitor \
		--port $(PORT) \
		--timestamp

list-boards:
	@arduino-cli board list
