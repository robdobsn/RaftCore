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
CFLAGS = -Wall -std=c++20 -g -DRAFT_CORE -DRAFT_RUNTIME_DECODE -DRAFT_RUNTIME_DECODE_CODE -DRAFT_RUNTIME_DECODE_VERIFY

# Each source is compiled to its own object in obj/ with a dependency file (obj/....d), so a change to any
# header rebuilds exactly what uses it. Objects are shared between tests
OBJDIR = obj
DEPFLAGS = -MMD -MP

CORE = ../components/core
COMMS = ../components/comms

# Generated device type records (see test_device_type_records below)
# SpiramAwareAllocator host tests: the real header with the ESP SDK shimmed, built once per configuration
# (name:IDF major:config:PSRAM bytes:PSRAM allocation fails:expect PSRAM used)
SPIRAM_TEST_DIR = spiram_allocator
SPIRAM_VARIANTS = idf6_modern:6:CONFIG_SPIRAM:8388608:0:1 idf5_modern:5:CONFIG_SPIRAM:8388608:0:1 \
  idf4_legacy:4:CONFIG_ESP32_SPIRAM_SUPPORT:8388608:0:1 idf6_legacy_alias:6:CONFIG_ESP32_SPIRAM_SUPPORT:8388608:0:1 \
  idf6_missing_psram:6:CONFIG_SPIRAM:0:0:0 idf6_psram_failure:6:CONFIG_SPIRAM:8388608:1:1 \
  idf6_psram_disabled:6:NONE:0:0:0 host_no_esp:0:NONE:0:0:0

DEV_TYPE_GEN_DIR = gen
# RaftCore's records (one file per record) assembled into one catalogue file, in key order, for the tests that read one
DEV_TYPE_RECORDS_DIR = ../devtypes/records
DEV_TYPE_RECORD_FILES = $(wildcard $(DEV_TYPE_RECORDS_DIR)/*.json)
DEV_TYPE_CATALOGUE = $(DEV_TYPE_GEN_DIR)/DeviceTypeRecords.json
DEV_TYPE_GEN_HEADER = $(DEV_TYPE_GEN_DIR)/DeviceTypeRecords_generated.h
CATALOGUE_INDEX_EXPECTED = $(DEV_TYPE_GEN_DIR)/catalogue_index_expected.json

# Reduced catalogue test: built with half the catalogue compiled in, the other half supplied as a device type file
REDUCED_GEN_DIR = gen_reduced
REDUCED_HEADER = $(REDUCED_GEN_DIR)/DeviceTypeRecords_generated.h
REDUCED_FILE = $(REDUCED_GEN_DIR)/file.json
REDUCED_OBJDIR = $(OBJDIR)/reduced
REDUCED_OBJS = $(REDUCED_OBJDIR)/test_reduced_catalogue.o $(REDUCED_OBJDIR)/DeviceTypeRecords.o \
  $(REDUCED_OBJDIR)/DeviceTypeGrammar.o $(REDUCED_OBJDIR)/DeviceTypeFileLoader.o $(REDUCED_OBJDIR)/DeviceTypeRuntimeDecoder.o $(REDUCED_OBJDIR)/DeviceTypeCodeInterpreter.o $(REDUCED_OBJDIR)/DeviceTypeDecodeVerifier.o

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
SOURCES_test_device_settings = test_device_settings.cpp $(COMMON_SOURCES)
SOURCES_test_device_type_settings = test_device_type_settings.cpp $(COMMON_SOURCES)
SOURCES_test_device_names = test_device_names.cpp $(COMMON_SOURCES)
SOURCES_test_device_data_subscribers = test_device_data_subscribers.cpp $(COMMON_SOURCES)
SOURCES_test_bus_scan_schedule = test_bus_scan_schedule.cpp $(COMMON_SOURCES)
SOURCES_test_bus_ident = test_bus_ident.cpp $(CORE)/Bus/DeviceStatus.cpp $(COMMON_SOURCES)
SOURCES_test_device_poll_timing = test_device_poll_timing.cpp $(CORE)/Bus/DeviceStatus.cpp $(COMMON_SOURCES)
SOURCES_test_device_poll_scheduler = test_device_poll_scheduler.cpp $(CORE)/Bus/DeviceStatus.cpp $(COMMON_SOURCES)
SOURCES_test_poll_plan = test_poll_plan.cpp $(CORE)/Bus/DeviceStatus.cpp $(CORE)/Bus/BusRequestInfo.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeGrammar.cpp $(CORE)/DeviceTypes/DeviceTypeRuntimeDecoder.cpp $(CORE)/DeviceTypes/DeviceTypeCodeInterpreter.cpp $(CORE)/DeviceTypes/DeviceTypeDecodeVerifier.cpp $(COMMON_SOURCES)
SOURCES_test_device_type_records = test_device_type_records.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeGrammar.cpp $(CORE)/DeviceTypes/DeviceTypeRuntimeDecoder.cpp $(CORE)/DeviceTypes/DeviceTypeCodeInterpreter.cpp $(CORE)/DeviceTypes/DeviceTypeDecodeVerifier.cpp $(COMMON_SOURCES)
SOURCES_test_device_type_table = test_device_type_table.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeGrammar.cpp $(CORE)/DeviceTypes/DeviceTypeRuntimeDecoder.cpp $(CORE)/DeviceTypes/DeviceTypeCodeInterpreter.cpp $(CORE)/DeviceTypes/DeviceTypeDecodeVerifier.cpp $(COMMON_SOURCES)
SOURCES_test_device_type_grammar = test_device_type_grammar.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeGrammar.cpp $(CORE)/DeviceTypes/DeviceTypeRuntimeDecoder.cpp $(CORE)/DeviceTypes/DeviceTypeCodeInterpreter.cpp $(CORE)/DeviceTypes/DeviceTypeDecodeVerifier.cpp $(CORE)/Bus/BusRequestInfo.cpp $(COMMON_SOURCES)
SOURCES_test_device_type_policy = test_device_type_policy.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeGrammar.cpp $(CORE)/DeviceTypes/DeviceTypeRuntimeDecoder.cpp $(CORE)/DeviceTypes/DeviceTypeCodeInterpreter.cpp $(CORE)/DeviceTypes/DeviceTypeDecodeVerifier.cpp $(COMMON_SOURCES)
SOURCES_test_device_type_file = test_device_type_file.cpp $(CORE)/DeviceTypes/DeviceTypeFileLoader.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeGrammar.cpp $(CORE)/DeviceTypes/DeviceTypeRuntimeDecoder.cpp $(CORE)/DeviceTypes/DeviceTypeCodeInterpreter.cpp $(CORE)/DeviceTypes/DeviceTypeDecodeVerifier.cpp $(CORE)/MiniHDLC/MiniHDLC.cpp $(COMMON_SOURCES)
SOURCES_test_catalogue_index = test_catalogue_index.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeGrammar.cpp $(CORE)/DeviceTypes/DeviceTypeRuntimeDecoder.cpp $(CORE)/DeviceTypes/DeviceTypeCodeInterpreter.cpp $(CORE)/DeviceTypes/DeviceTypeDecodeVerifier.cpp $(COMMON_SOURCES)
SOURCES_test_runtime_decoder = test_runtime_decoder.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeGrammar.cpp $(CORE)/DeviceTypes/DeviceTypeRuntimeDecoder.cpp $(CORE)/DeviceTypes/DeviceTypeCodeInterpreter.cpp $(CORE)/DeviceTypes/DeviceTypeDecodeVerifier.cpp $(CORE)/DeviceTypes/DeviceTypeFileLoader.cpp $(CORE)/MiniHDLC/MiniHDLC.cpp $(COMMON_SOURCES)
SOURCES_test_protocol_exchange = test_protocol_exchange.cpp $(COMMS_SOURCES) $(COMMON_SOURCES)
SOURCES_test_thread_safe_queue = test_thread_safe_queue.cpp $(COMMON_SOURCES)
SOURCES_test_espnow_config = test_espnow_config.cpp $(ESPNOW_DIR)/ESPNowConfig.cpp $(COMMON_SOURCES)

# Tests run by "make check"
TESTS = test_raft_json test_raft_utils test_device_names test_device_settings test_device_type_settings test_device_data_subscribers test_device_type_records \
  test_device_type_table test_device_type_grammar test_device_type_policy test_device_type_file test_catalogue_index test_runtime_decoder test_bus_ident test_bus_scan_schedule test_device_poll_timing test_device_poll_scheduler test_poll_plan test_protocol_exchange test_thread_safe_queue

# Extra device type catalogues for test_device_type_records to sweep, e.g.
#   make check DEV_TYPE_CATALOGUES="../../RaftI2C/linux_unit_tests/TestDevTypeRecs.json"
DEV_TYPE_CATALOGUES ?=

# Object file for a source: obj/ plus its path with any leading ../ removed
objs = $(addprefix $(OBJDIR)/,$(subst ../,,$(1:.cpp=.o)))

.PHONY: all check clean spiram_allocator_tests bench

all: $(TESTS)

check: $(DEV_TYPE_CATALOGUE) $(TESTS) $(CATALOGUE_INDEX_EXPECTED) test_reduced_catalogue
	@failed=0; \
	for t in $(TESTS); do \
		echo "===== $$t"; \
		if [ "$$t" = "test_device_type_records" ]; then ./$$t $(DEV_TYPE_CATALOGUES) || failed=1; \
		elif [ "$$t" = "test_catalogue_index" ]; then ./$$t $(CATALOGUE_INDEX_EXPECTED) || failed=1; \
		elif [ "$$t" = "test_device_type_file" ]; then ./$$t $(DEV_TYPE_CATALOGUE) fixtures/example-devlib.json || failed=1; \
		elif [ "$$t" = "test_runtime_decoder" ]; then ./$$t $(DEV_TYPE_CATALOGUE) || failed=1; \
		elif [ "$$t" = "test_device_type_grammar" ]; then ./$$t ../scripts/tests/grammar_vectors.json $(DEV_TYPE_CATALOGUE) $(DEV_TYPE_CATALOGUES) || failed=1; \
		else ./$$t || failed=1; fi; \
	done; \
	echo "===== test_espnow_config"; \
	if [ -f $(ESPNOW_DIR)/ESPNowConfig.cpp ]; then \
		$(MAKE) --no-print-directory test_espnow_config && ./test_espnow_config || failed=1; \
	else \
		echo "test_espnow_config SKIPPED ($(ESPNOW_DIR) not present)"; \
	fi; \
	echo "===== test_reduced_catalogue"; \
	./test_reduced_catalogue $(DEV_TYPE_CATALOGUE) $(REDUCED_FILE) $(CATALOGUE_INDEX_EXPECTED) || failed=1; \
	$(MAKE) --no-print-directory spiram_allocator_tests || failed=1; \
	echo "===== scripts/tests (device type generator)"; \
	python3 -m unittest discover -s ../scripts/tests -p "test_*.py" || failed=1; \
	if [ $$failed -ne 0 ]; then echo "===== SOME TESTS FAILED"; exit 1; fi; \
	echo "===== ALL TESTS PASSED"

# Link each test from its objects
spiram_allocator_tests:
	@echo "===== test_spiram_allocator"; \
	mkdir -p $(OBJDIR)/spiram_allocator; failed=0; \
	for v in $(SPIRAM_VARIANTS); do \
		set -- $$(echo $$v | tr ':' ' '); \
		flags="-DTEST_IDF_MAJOR=$$2 -DTEST_PSRAM_BYTES=$$4 -DTEST_ALLOCATION_FAILS=$$5 -DTEST_EXPECT_SPIRAM=$$6"; \
		if [ $$2 -gt 0 ]; then flags="$$flags -DESP_PLATFORM=1"; fi; \
		if [ "$$3" != "NONE" ]; then flags="$$flags -DTEST_$$3=1"; fi; \
		out=$(OBJDIR)/spiram_allocator/$$1; \
		$(CC) -std=c++20 -Wall -Wextra -g $$flags -I$(CORE)/Utils -I$(SPIRAM_TEST_DIR)/sdk \
			-o $$out $(SPIRAM_TEST_DIR)/test_spiram_allocator.cpp || { failed=1; continue; }; \
		printf "%-22s " $$1; $$out || failed=1; \
	done; \
	if [ $$failed -ne 0 ]; then echo "test_spiram_allocator FAILED"; exit 1; fi; \
	echo "test_spiram_allocator all $(words $(SPIRAM_VARIANTS)) configurations passed"

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

# The RaftCore catalogue as one file, from the record files
$(DEV_TYPE_CATALOGUE): $(DEV_TYPE_RECORD_FILES) ../scripts/AssembleDeviceTypes.py
	mkdir -p $(DEV_TYPE_GEN_DIR)
	python3 ../scripts/AssembleDeviceTypes.py --raftcore .. --all --out $@

# Device type records: the built-in records are generated from the RaftCore catalogue by the real generator
$(DEV_TYPE_GEN_HEADER): $(DEV_TYPE_CATALOGUE) ../scripts/ProcessDevTypeJsonToC.py ../scripts/DecodeGenerator.py ../scripts/AssembleDeviceTypes.py
	mkdir -p $(DEV_TYPE_GEN_DIR)
	python3 ../scripts/ProcessDevTypeJsonToC.py $(DEV_TYPE_CATALOGUE) $(DEV_TYPE_GEN_HEADER) \
		$(DEV_TYPE_GEN_DIR)/DevicePollRecords_generated.h > $(DEV_TYPE_GEN_DIR)/generator.log

$(call objs,test_device_type_records.cpp test_device_type_table.cpp test_catalogue_index.cpp test_runtime_decoder.cpp test_device_type_grammar.cpp test_device_type_policy.cpp test_device_type_file.cpp test_poll_plan.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp): $(DEV_TYPE_GEN_HEADER)

# The address map and scan lists the generator used to derive from the catalogue (for test_catalogue_index)
$(CATALOGUE_INDEX_EXPECTED): $(DEV_TYPE_CATALOGUE) ../scripts/tests/reference_catalogue_index.py
	mkdir -p $(DEV_TYPE_GEN_DIR)
	python3 ../scripts/tests/reference_catalogue_index.py $(DEV_TYPE_CATALOGUE) $@

# The reduced catalogue (compiled into test_reduced_catalogue) and the file with the rest
$(REDUCED_HEADER) $(REDUCED_FILE): $(DEV_TYPE_CATALOGUE) ../scripts/tests/split_catalogue.py ../scripts/ProcessDevTypeJsonToC.py ../scripts/DecodeGenerator.py
	mkdir -p $(REDUCED_GEN_DIR)
	python3 ../scripts/tests/split_catalogue.py $(DEV_TYPE_CATALOGUE) $(REDUCED_GEN_DIR)/reduced.json $(REDUCED_FILE)
	python3 ../scripts/ProcessDevTypeJsonToC.py $(REDUCED_GEN_DIR)/reduced.json $(REDUCED_HEADER) \
		$(REDUCED_GEN_DIR)/DevicePollRecords_generated.h > $(REDUCED_GEN_DIR)/generator.log

$(REDUCED_OBJDIR)/test_reduced_catalogue.o: test_reduced_catalogue.cpp $(REDUCED_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -I$(REDUCED_GEN_DIR) $(INCLUDES) -c -o $@ $<

$(REDUCED_OBJDIR)/%.o: $(CORE)/DeviceTypes/%.cpp $(REDUCED_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -I$(REDUCED_GEN_DIR) $(INCLUDES) -c -o $@ $<

test_reduced_catalogue: $(REDUCED_OBJS) $(call objs,$(CORE)/MiniHDLC/MiniHDLC.cpp $(COMMON_SOURCES))
	$(CC) $(CFLAGS) -o $@ $^

-include $(shell find $(OBJDIR) -name '*.d' 2>/dev/null)


# Runtime decoder timing against the compiled decoders (not part of check): make bench [BENCH_ARGS="<record> <iterations>"]
BENCH_FLAGS = -std=c++20 -O2 -DRAFT_CORE -DRAFT_RUNTIME_DECODE -DRAFT_RUNTIME_DECODE_CODE
BENCH_SOURCES = bench_runtime_decoder.cpp $(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeGrammar.cpp \
  $(CORE)/DeviceTypes/DeviceTypeRuntimeDecoder.cpp $(CORE)/DeviceTypes/DeviceTypeCodeInterpreter.cpp $(COMMON_SOURCES)
bench_runtime_decoder: $(BENCH_SOURCES) $(DEV_TYPE_GEN_HEADER) $(wildcard $(CORE)/DeviceTypes/*.h)
	$(CC) $(BENCH_FLAGS) $(INCLUDES) -o $@ $(BENCH_SOURCES)
bench: bench_runtime_decoder
	./bench_runtime_decoder $(BENCH_ARGS)

clean:
	rm -f $(TESTS) test_espnow_config test_reduced_catalogue bench_runtime_decoder linux_unit_tests arduino_stubs.o *.d
	rm -rf $(OBJDIR) $(DEV_TYPE_GEN_DIR) $(REDUCED_GEN_DIR)
