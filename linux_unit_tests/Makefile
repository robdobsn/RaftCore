#####################################################################################################################
#
# RaftCore Linux unit tests
#
#   make check     build and run every test (exit code non-zero if any test fails) - this is what CI runs
#   make           build every test
#   make <test>    build one test, e.g. make test_raft_json
#   make clean
#
# Each test is a separate program using the harness in testhdr.h. See README.md for adding a test.
#
#####################################################################################################################

CC = g++
CFLAGS = -Wall -std=c++20 -g -DRAFT_CORE

CORE = ../components/core
COMMS = ../components/comms

INCLUDES = -I. -I../unit_tests/main \
  -I$(CORE)/ArduinoUtils -I$(CORE)/RaftJson -I$(CORE)/Utils -I$(CORE)/NamedValueProvider \
  -I$(CORE)/SupervisorStats -I$(CORE)/DebugGlobals -I$(CORE)/SysMod -I$(CORE)/RaftDevice -I$(CORE)/Bus \
  -I$(CORE)/DeviceManager -I$(CORE)/DeviceTypes -I$(CORE)/APICommon -I$(CORE)/FileSystem \
  -I$(CORE)/RestAPIEndpoints -I$(CORE)/MiniHDLC -I$(CORE)/ThreadSafeQueue -I$(CORE)/SysManager \
  -I$(CORE)/SysTypes -I$(CORE)/RingBuffer \
  -I$(COMMS)/FileStreamProtocols -I$(COMMS)/ProtocolExchange -I$(COMMS)/ProtocolBase -I$(COMMS)/ProtocolRawMsg \
  -I$(COMMS)/CommsCoreIF -I$(COMMS)/CommsChannelMsg -I$(COMMS)/CommsChannels -I$(COMMS)/ProtocolRICSerial \
  -I$(COMMS)/ProtocolRICFrame -I$(COMMS)/ProtocolRICJSON -I$(COMMS)/RICRESTMsg -I$(COMMS)/CommsBridgeMsg

# Platform and utility sources most tests need
COMMON_SOURCES = \
  $(CORE)/ArduinoUtils/ArduinoWString.cpp \
  $(CORE)/ArduinoUtils/ArduinoTime.cpp \
  $(CORE)/Utils/RaftUtils.cpp \
  $(CORE)/Utils/PlatformUtils.cpp \
  $(CORE)/Utils/RaftThreading.cpp

# Comms sources for ProtocolExchange
COMMS_SOURCES = \
  $(COMMS)/ProtocolExchange/ProtocolExchange.cpp \
  $(COMMS)/ProtocolExchange/FileStreamSession.cpp \
  $(COMMS)/RICRESTMsg/RICRESTMsg.cpp \
  $(COMMS)/ProtocolRICSerial/ProtocolRICSerial.cpp \
  $(COMMS)/ProtocolRICFrame/ProtocolRICFrame.cpp \
  $(COMMS)/ProtocolRICJSON/ProtocolRICJSON.cpp \
  $(COMMS)/FileStreamProtocols/FileStreamBase.cpp \
  $(COMMS)/FileStreamProtocols/StreamDatagramProtocol.cpp \
  $(COMMS)/FileStreamProtocols/FileUploadHTTPProtocol.cpp \
  $(COMMS)/FileStreamProtocols/FileDownloadOKTOProtocol.cpp \
  $(COMMS)/FileStreamProtocols/FileUploadOKTOProtocol.cpp \
  $(CORE)/SysMod/RaftSysMod.cpp \
  $(CORE)/RestAPIEndpoints/RestAPIEndpointManager.cpp \
  $(CORE)/MiniHDLC/MiniHDLC.cpp \
  $(CORE)/FileSystem/FileSystemChunker.cpp \
  $(CORE)/FileSystem/FileSystem.cpp \
  linux_stubs.cpp

# Tests run by "make check"
TESTS = test_raft_json test_raft_utils test_device_names test_device_data_subscribers test_device_type_records \
  test_protocol_exchange

# Extra device type catalogues for test_device_type_records to sweep, e.g.
#   make check DEV_TYPE_CATALOGUES="../../RaftI2C/linux_unit_tests/TestDevTypeRecs.json"
DEV_TYPE_CATALOGUES ?=

# test_espnow_config tests code that is not yet on RaftSysMods main (branch feature/ESPNow-as-first-class-protocol).
# "make check" runs it only when that code is present in ../../RaftSysMods, and otherwise reports it as skipped
ESPNOW_DIR = ../../RaftSysMods/components/ESPNowManager

.PHONY: all check clean

all: $(TESTS)

check: $(TESTS)
	@failed=0; \
	for t in $(TESTS); do \
		echo "===== $$t"; \
		if [ "$$t" = "test_device_type_records" ]; then ./$$t $(DEV_TYPE_CATALOGUES) || failed=1; \
		else ./$$t || failed=1; fi; \
	done; \
	echo "===== test_espnow_config"; \
	if [ -f $(ESPNOW_DIR)/ESPNowConfig.cpp ]; then \
		$(MAKE) --no-print-directory test_espnow_config && ./test_espnow_config || failed=1; \
	else \
		echo "test_espnow_config SKIPPED ($(ESPNOW_DIR) not present)"; \
	fi; \
	if [ $$failed -ne 0 ]; then echo "===== SOME TESTS FAILED"; exit 1; fi; \
	echo "===== ALL TESTS PASSED"

test_raft_json: test_raft_json.cpp testhdr.h $(CORE)/RaftJson/RaftJson.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_raft_json.cpp $(COMMON_SOURCES)

test_raft_utils: test_raft_utils.cpp testhdr.h $(CORE)/Utils/RaftUtils.cpp $(CORE)/Utils/RaftUtils.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_raft_utils.cpp $(COMMON_SOURCES)

test_device_names: test_device_names.cpp testhdr.h $(CORE)/DeviceManager/DeviceNames.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_device_names.cpp $(COMMON_SOURCES)

test_device_data_subscribers: test_device_data_subscribers.cpp testhdr.h $(CORE)/DeviceManager/DeviceDataSubscribers.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_device_data_subscribers.cpp $(COMMON_SOURCES)

test_protocol_exchange: test_protocol_exchange.cpp testhdr.h $(COMMS_SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_protocol_exchange.cpp $(COMMS_SOURCES) $(COMMON_SOURCES)

# Device type records: the built-in records are generated from the RaftCore catalogue by the real generator
DEV_TYPE_GEN_DIR = gen
DEV_TYPE_CATALOGUE = ../devtypes/DeviceTypeRecords.json
DEV_TYPE_GEN_HEADER = $(DEV_TYPE_GEN_DIR)/DeviceTypeRecords_generated.h

$(DEV_TYPE_GEN_HEADER): $(DEV_TYPE_CATALOGUE) ../scripts/ProcessDevTypeJsonToC.py ../scripts/DecodeGenerator.py
	mkdir -p $(DEV_TYPE_GEN_DIR)
	python3 ../scripts/ProcessDevTypeJsonToC.py $(DEV_TYPE_CATALOGUE) $(DEV_TYPE_GEN_HEADER) \
		$(DEV_TYPE_GEN_DIR)/DevicePollRecords_generated.h > $(DEV_TYPE_GEN_DIR)/generator.log

test_device_type_records: test_device_type_records.cpp testhdr.h $(DEV_TYPE_GEN_HEADER) \
		$(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.h
	$(CC) $(CFLAGS) -I$(DEV_TYPE_GEN_DIR) $(INCLUDES) -o $@ test_device_type_records.cpp \
		$(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(COMMON_SOURCES)

test_espnow_config: test_espnow_config.cpp $(ESPNOW_DIR)/ESPNowConfig.h $(ESPNOW_DIR)/ESPNowConfig.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -I$(ESPNOW_DIR) -o $@ test_espnow_config.cpp $(ESPNOW_DIR)/ESPNowConfig.cpp \
		$(COMMON_SOURCES)

clean:
	rm -f $(TESTS) test_espnow_config linux_unit_tests arduino_stubs.o
	rm -rf $(DEV_TYPE_GEN_DIR)
