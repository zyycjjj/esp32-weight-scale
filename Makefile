.PHONY: flash monitor flash_monitor port_check port_free

AUTO_PORT := $(shell ls -1 /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial* 2>/dev/null | head -n 1)
PORT ?= $(AUTO_PORT)
BAUD ?= 115200
MONITOR_FILTER ?= send_on_enter

export PLATFORMIO_CORE_DIR := ./.platformio-core
export PLATFORMIO_PACKAGES_DIR := /Users/yangzhang/.platformio/packages
export PLATFORMIO_PLATFORMS_DIR := /Users/yangzhang/.platformio/platforms

flash:
	$(MAKE) port_free
	pio run -t upload --upload-port $(PORT)

monitor:
	pio device monitor --port $(PORT) --baud $(BAUD) --filter $(MONITOR_FILTER)

flash_monitor:
	$(MAKE) port_free
	pio run -t upload --upload-port $(PORT) && pio device monitor --port $(PORT) --baud $(BAUD) --filter $(MONITOR_FILTER)

port_check:
	@echo "PORT=$(PORT)"
	@test -n "$(PORT)" || (echo "No serial port found. Plug ESP-BOX and re-run, or set PORT=/dev/cu.usbmodemXXXX"; exit 1)
	@lsof $(PORT) || true

port_free:
	@test -n "$(PORT)" || exit 0
	@pids=$$(lsof -t $(PORT) 2>/dev/null); \
	if [ -n "$$pids" ]; then \
		echo "Killing processes using $(PORT): $$pids"; \
		kill $$pids || true; \
		sleep 0.2; \
	fi
