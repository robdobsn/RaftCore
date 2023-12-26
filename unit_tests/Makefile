ROOTDIR = $(realpath $(CURDIR)/..)
WORKING_DIR = $(realpath $(CURDIR))
DOCKER ?= docker run --rm -v $(ROOTDIR):$(ROOTDIR) -w $(WORKING_DIR) espressif/idf:v5.1.2
CMD ?= idf.py -B ./build build

all: build

build: Makefile $(wildcard ./*) $(wildcard components/core/*)
	$(DOCKER) $(CMD)

clean:
	$(DOCKER) rm -rf build sdkconfig || true

PORT ?= COM5

flashwsl: build
	python.exe -m esptool -p $(PORT) -b 460800 --before default_reset --after hard_reset --chip esp32  write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0xe000 build/ota_data_initial.bin 0x10000 build/unittests.bin
	python.exe ../scripts/SerialMonitor.py $(PORT) -g
