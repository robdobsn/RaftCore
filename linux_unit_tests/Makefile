# Compiler
CC = g++

# Compiler flags
CFLAGS = -Wall -std=c++20 -lc -g -DRAFT_CORE

# Include paths
INCLUDES = -I../unit_tests/main \
  -I../components/core/ArduinoUtils \
  -I../components/core/RaftJson \
  -I../components/core/Utils \
  -I../components/core/NamedValueProvider \
  -I../components/core/SupervisorStats \
  -I../components/core/DebugGlobals \
  -I../components/core/SysMod \
  -I../components/core/RaftDevice \
  -I../components/core/Bus \
  -I../components/core/DeviceManager \
  -I../components/core/DeviceTypes \
  -I../components/core/APICommon \
  -I../components/core/FileSystem \
  -I../components/core/RestAPIEndpoints \
  -I../components/comms/FileStreamProtocols \
  -I../components/comms/ProtocolExchange \
  -I../components/comms/ProtocolBase \
  -I../components/comms/ProtocolRawMsg \
  -I../components/comms/CommsCoreIF \
  -I../components/comms/CommsChannelMsg \
  -I../components/comms/CommsChannels \
  -I../components/comms/ProtocolRICSerial \
  -I../components/comms/ProtocolRICFrame \
  -I../components/comms/ProtocolRICJSON \
  -I../components/comms/RICRESTMsg \
  -I../components/comms/CommsBridgeMsg \
  -I../components/core/MiniHDLC \
  -I../components/core/ThreadSafeQueue \
  -I../components/core/SysManager \
  -I../components/core/SysTypes \
  -I../components/core/RingBuffer \
  -I.

# Source files
SOURCES = main.cpp \
  ../components/core/ArduinoUtils/ArduinoWString.cpp \
  ../components/core/Utils/RaftUtils.cpp \
  ../components/core/Utils/PlatformUtils.cpp \
  ../components/core/Utils/RaftThreading.cpp \
  ../components/comms/ProtocolExchange/ProtocolExchange.cpp \
  ../components/comms/ProtocolExchange/FileStreamSession.cpp \
  ../components/core/SysMod/RaftSysMod.cpp \
  ../components/comms/RICRESTMsg/RICRESTMsg.cpp \
  ../components/comms/ProtocolRICSerial/ProtocolRICSerial.cpp \
  ../components/comms/ProtocolRICFrame/ProtocolRICFrame.cpp \
  ../components/comms/ProtocolRICJSON/ProtocolRICJSON.cpp \
  ../components/core/RestAPIEndpoints/RestAPIEndpointManager.cpp \
  ../components/comms/FileStreamProtocols/FileStreamBase.cpp \
  ../components/comms/FileStreamProtocols/StreamDatagramProtocol.cpp \
  ../components/comms/FileStreamProtocols/FileUploadHTTPProtocol.cpp \
  ../components/comms/FileStreamProtocols/FileDownloadOKTOProtocol.cpp \
  ../components/comms/FileStreamProtocols/FileUploadOKTOProtocol.cpp \
  ../components/core/MiniHDLC/MiniHDLC.cpp \
  ../components/core/ArduinoUtils/ArduinoTime.cpp \
  ../components/core/FileSystem/FileSystemChunker.cpp \
  ../components/core/FileSystem/FileSystem.cpp \
  linux_stubs.cpp

# Output binary
OUTPUT = linux_unit_tests

# test_espnow_config needs RaftSysMods ESPNowManager (not yet on main) - build it explicitly with "make test_espnow_config"
all: $(OUTPUT) test_device_names test_device_type_records

# Tests that gate automation (CI): each exits non-zero on failure. Extra device type catalogues can be
# swept too, e.g. make check DEV_TYPE_CATALOGUES="../../RaftI2C/linux_unit_tests/TestDevTypeRecs.json"
DEV_TYPE_CATALOGUES ?=
.PHONY: check
check: test_device_names test_device_type_records
	./test_device_names
	./test_device_type_records $(DEV_TYPE_CATALOGUES)

$(OUTPUT): $(SOURCES) ../components/core/RaftJson/RaftJson.h ../components/core/Utils/RaftUtils.h ../components/core/DeviceManager/DeviceDataSubscribers.h DeviceDataSubscribersTest.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $(OUTPUT) $(SOURCES)

# Arduino stub object for missing itoa/utoa on Linux
arduino_stubs.o: arduino_stubs.c
	gcc -c -o $@ $<

# Device names map unit test (standalone, minimal dependencies)
test_device_names: test_device_names.cpp DeviceNamesTest.h arduino_stubs.o
	$(CC) $(CFLAGS) -I../components/core/ArduinoUtils -I../components/core/RaftJson \
		-I../components/core/RaftDevice -I../components/core/Bus -I. \
		-o $@ test_device_names.cpp \
		../components/core/ArduinoUtils/ArduinoWString.cpp \
		../components/core/ArduinoUtils/ArduinoTime.cpp \
		../components/core/ArduinoUtils/ArduinoStdlibNonISO.c \
		arduino_stubs.o

# ESPNow config parser unit test (standalone, target-agnostic)
test_espnow_config: test_espnow_config.cpp ../../RaftSysMods/components/ESPNowManager/ESPNowConfig.h ../../RaftSysMods/components/ESPNowManager/ESPNowConfig.cpp arduino_stubs.o
	$(CC) $(CFLAGS) -I../components/core/ArduinoUtils -I../components/core/RaftJson \
		-I../../RaftSysMods/components/ESPNowManager -I. \
		-o $@ test_espnow_config.cpp \
		../../RaftSysMods/components/ESPNowManager/ESPNowConfig.cpp \
		../components/core/ArduinoUtils/ArduinoWString.cpp \
		../components/core/ArduinoUtils/ArduinoTime.cpp \
		../components/core/ArduinoUtils/ArduinoStdlibNonISO.c \
		arduino_stubs.o

# Device type records test: detection value parsing, checked against every built-in record. The
# built-in records are generated from the RaftCore catalogue by the real generator script
DEV_TYPE_GEN_DIR = gen
DEV_TYPE_CATALOGUE = ../devtypes/DeviceTypeRecords.json
DEV_TYPE_GEN_HEADER = $(DEV_TYPE_GEN_DIR)/DeviceTypeRecords_generated.h

$(DEV_TYPE_GEN_HEADER): $(DEV_TYPE_CATALOGUE) ../scripts/ProcessDevTypeJsonToC.py ../scripts/DecodeGenerator.py
	mkdir -p $(DEV_TYPE_GEN_DIR)
	python3 ../scripts/ProcessDevTypeJsonToC.py $(DEV_TYPE_CATALOGUE) $(DEV_TYPE_GEN_HEADER) \
		$(DEV_TYPE_GEN_DIR)/DevicePollRecords_generated.h > $(DEV_TYPE_GEN_DIR)/generator.log

test_device_type_records: test_device_type_records.cpp $(DEV_TYPE_GEN_HEADER) \
		../components/core/DeviceTypes/DeviceTypeRecords.cpp ../components/core/DeviceTypes/DeviceTypeRecords.h
	$(CC) $(CFLAGS) -I$(DEV_TYPE_GEN_DIR) -I. -I../components/core/ArduinoUtils -I../components/core/RaftJson \
		-I../components/core/Utils -I../components/core/Bus -I../components/core/DeviceTypes \
		-I../components/core/RaftDevice -I../components/core/DebugGlobals \
		-o $@ test_device_type_records.cpp \
		../components/core/DeviceTypes/DeviceTypeRecords.cpp \
		../components/core/Utils/RaftUtils.cpp \
		../components/core/Utils/RaftThreading.cpp \
		../components/core/Utils/PlatformUtils.cpp \
		../components/core/ArduinoUtils/ArduinoWString.cpp \
		../components/core/ArduinoUtils/ArduinoTime.cpp

clean:
	rm -f $(OUTPUT) test_device_names test_espnow_config test_device_type_records arduino_stubs.o
	rm -rf $(DEV_TYPE_GEN_DIR)
