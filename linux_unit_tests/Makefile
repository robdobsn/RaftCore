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

# Each source is compiled to its own object in obj/ with a dependency file (obj/....d), so a change to any
# header rebuilds exactly what uses it. Objects are shared between tests
OBJDIR = obj
DEPFLAGS = -MMD -MP

CORE = ../components/core
COMMS = ../components/comms

# Generated device type records (see test_device_type_records below)
DEV_TYPE_GEN_DIR = gen
DEV_TYPE_CATALOGUE = ../devtypes/DeviceTypeRecords.json
DEV_TYPE_GEN_HEADER = $(DEV_TYPE_GEN_DIR)/DeviceTypeRecords_generated.h
CATALOGUE_INDEX_EXPECTED = $(DEV_TYPE_GEN_DIR)/catalogue_index_expected.json

# test_espnow_config tests code that is not yet on RaftSysMods main (branch feature/ESPNow-as-first-class-protocol).
# "make check" runs it only when that code is present in ../../RaftSysMods, and otherwise reports it as skipped
ESPNOW_DIR = ../../RaftSysMods/components/ESPNowManager

INCLUDES = -I. -I../unit_tests/main -I$(DEV_TYPE_GEN_DIR) -I$(ESPNOW_DIR) \
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

# Sources for each test
SOURCES_test_raft_json = test_raft_json.cpp $(COMMON_SOURCES)
SOURCES_test_raft_utils = test_raft_utils.cpp $(COMMON_SOURCES)
SOURCES_test_device_names = test_device_names.cpp $(COMMON_SOURCES)
SOURCES_test_device_data_subscribers = test_device_data_subscribers.cpp $(COMMON_SOURCES)
SOURCES_test_bus_ident = test_bus_ident.cpp $(CORE)/Bus/DeviceStatus.cpp $(COMMON_SOURCES)
SOURCES_test_device_poll_timing = test_device_poll_timing.cpp $(CORE)/Bus/DeviceStatus.cpp $(COMMON_SOURCES)
SOURCES_test_device_poll_scheduler = test_device_poll_scheduler.cpp $(CORE)/Bus/DeviceStatus.cpp $(COMMON_SOURCES)
SOURCES_test_device_type_records = test_device_type_records.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeGrammar.cpp $(COMMON_SOURCES)
SOURCES_test_device_type_table = test_device_type_table.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeGrammar.cpp $(COMMON_SOURCES)
SOURCES_test_device_type_grammar = test_device_type_grammar.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeGrammar.cpp $(CORE)/Bus/BusRequestInfo.cpp $(COMMON_SOURCES)
SOURCES_test_device_type_policy = test_device_type_policy.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeGrammar.cpp $(COMMON_SOURCES)
SOURCES_test_device_type_file = test_device_type_file.cpp $(CORE)/DeviceTypes/DeviceTypeFileLoader.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeGrammar.cpp $(CORE)/MiniHDLC/MiniHDLC.cpp $(COMMON_SOURCES)
SOURCES_test_catalogue_index = test_catalogue_index.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeGrammar.cpp $(COMMON_SOURCES)
SOURCES_test_protocol_exchange = test_protocol_exchange.cpp $(COMMS_SOURCES) $(COMMON_SOURCES)
SOURCES_test_thread_safe_queue = test_thread_safe_queue.cpp $(COMMON_SOURCES)
SOURCES_test_espnow_config = test_espnow_config.cpp $(ESPNOW_DIR)/ESPNowConfig.cpp $(COMMON_SOURCES)

# Tests run by "make check"
TESTS = test_raft_json test_raft_utils test_device_names test_device_data_subscribers test_device_type_records \
  test_device_type_table test_device_type_grammar test_device_type_policy test_device_type_file test_catalogue_index test_bus_ident test_device_poll_timing test_device_poll_scheduler test_protocol_exchange test_thread_safe_queue

# Extra device type catalogues for test_device_type_records to sweep, e.g.
#   make check DEV_TYPE_CATALOGUES="../../RaftI2C/linux_unit_tests/TestDevTypeRecs.json"
DEV_TYPE_CATALOGUES ?=

# Object file for a source: obj/ plus its path with any leading ../ removed
objs = $(addprefix $(OBJDIR)/,$(subst ../,,$(1:.cpp=.o)))

.PHONY: all check clean

all: $(TESTS)

check: $(TESTS) $(CATALOGUE_INDEX_EXPECTED)
	@failed=0; \
	for t in $(TESTS); do \
		echo "===== $$t"; \
		if [ "$$t" = "test_device_type_records" ]; then ./$$t $(DEV_TYPE_CATALOGUES) || failed=1; \
		elif [ "$$t" = "test_catalogue_index" ]; then ./$$t $(CATALOGUE_INDEX_EXPECTED) || failed=1; \
		elif [ "$$t" = "test_device_type_file" ]; then ./$$t $(DEV_TYPE_CATALOGUE) fixtures/example-devlib.json || failed=1; \
		elif [ "$$t" = "test_device_type_grammar" ]; then ./$$t ../scripts/tests/grammar_vectors.json $(DEV_TYPE_CATALOGUE) $(DEV_TYPE_CATALOGUES) || failed=1; \
		else ./$$t || failed=1; fi; \
	done; \
	echo "===== test_espnow_config"; \
	if [ -f $(ESPNOW_DIR)/ESPNowConfig.cpp ]; then \
		$(MAKE) --no-print-directory test_espnow_config && ./test_espnow_config || failed=1; \
	else \
		echo "test_espnow_config SKIPPED ($(ESPNOW_DIR) not present)"; \
	fi; \
	echo "===== scripts/tests (device type generator)"; \
	python3 -m unittest discover -s ../scripts/tests -p "test_*.py" || failed=1; \
	if [ $$failed -ne 0 ]; then echo "===== SOME TESTS FAILED"; exit 1; fi; \
	echo "===== ALL TESTS PASSED"

# Link each test from its objects
.SECONDEXPANSION:
$(TESTS) test_espnow_config: $$(call objs,$$(SOURCES_$$@))
	$(CC) $(CFLAGS) -o $@ $^

# Compile: local sources, sources one level up (RaftCore) and two levels up (other repos)
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) $(INCLUDES) -c -o $@ $<

$(OBJDIR)/%.o: ../%.cpp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) $(INCLUDES) -c -o $@ $<

$(OBJDIR)/%.o: ../../%.cpp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) $(INCLUDES) -c -o $@ $<

# Device type records: the built-in records are generated from the RaftCore catalogue by the real generator
$(DEV_TYPE_GEN_HEADER): $(DEV_TYPE_CATALOGUE) ../scripts/ProcessDevTypeJsonToC.py ../scripts/DecodeGenerator.py
	mkdir -p $(DEV_TYPE_GEN_DIR)
	python3 ../scripts/ProcessDevTypeJsonToC.py $(DEV_TYPE_CATALOGUE) $(DEV_TYPE_GEN_HEADER) \
		$(DEV_TYPE_GEN_DIR)/DevicePollRecords_generated.h > $(DEV_TYPE_GEN_DIR)/generator.log

$(call objs,test_device_type_records.cpp test_device_type_table.cpp test_catalogue_index.cpp test_device_type_grammar.cpp test_device_type_policy.cpp test_device_type_file.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp): $(DEV_TYPE_GEN_HEADER)

# The address map and scan lists the generator used to derive from the catalogue (for test_catalogue_index)
$(CATALOGUE_INDEX_EXPECTED): $(DEV_TYPE_CATALOGUE) ../scripts/tests/reference_catalogue_index.py
	mkdir -p $(DEV_TYPE_GEN_DIR)
	python3 ../scripts/tests/reference_catalogue_index.py $(DEV_TYPE_CATALOGUE) $@

-include $(shell find $(OBJDIR) -name '*.d' 2>/dev/null)

clean:
	rm -f $(TESTS) test_espnow_config linux_unit_tests arduino_stubs.o *.d
	rm -rf $(OBJDIR) $(DEV_TYPE_GEN_DIR)
