.PHONY: flash monitor flash_monitor port_check port_free

PORT ?= /dev/cu.usbmodem11401
BAUD ?= 115200

export PLATFORMIO_CORE_DIR := ./.platformio-core
export PLATFORMIO_PACKAGES_DIR := /Users/yangzhang/.platformio/packages
export PLATFORMIO_PLATFORMS_DIR := /Users/yangzhang/.platformio/platforms

flash:
	$(MAKE) port_free
	pio run -t upload --upload-port $(PORT)

monitor:
	pio device monitor --port $(PORT) --baud $(BAUD)

flash_monitor:
	$(MAKE) port_free
	pio run -t upload --upload-port $(PORT) && pio device monitor --port $(PORT) --baud $(BAUD)

port_check:
	@echo "PORT=$(PORT)"
	@lsof $(PORT) || true

port_free:
	@pids=$$(lsof -t $(PORT) 2>/dev/null); \
	if [ -n "$$pids" ]; then \
		echo "Killing processes using $(PORT): $$pids"; \
		kill $$pids || true; \
		sleep 0.2; \
	fi
