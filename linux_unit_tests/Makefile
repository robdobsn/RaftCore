# Compiler
CC = g++

# Compiler flags
CFLAGS = -Wall -std=c++20 -g -DRAFT_CORE

# Include paths - Add linux_unit_tests directory first to ensure our stubs are found before the real files
INCLUDES = -I. -I../unit_tests/main -I../components/core/ArduinoUtils -I../components/core/RaftJson -I../components/core/Utils -I../components/core/DeviceTypes -I../components/core/Bus -I../components/core/RaftDevice

# Source files for main tests
MAIN_SOURCES = main.cpp ../components/core/ArduinoUtils/ArduinoWString.cpp ../components/core/Utils/RaftUtils.cpp ../components/core/Utils/PlatformUtils.cpp

# Source files for device type records test
DEVICE_TYPE_SOURCES = DeviceTypeRecordsTest.cpp ../components/core/ArduinoUtils/ArduinoWString.cpp ../components/core/Utils/RaftUtils.cpp ../components/core/Utils/PlatformUtils.cpp ../components/core/Utils/RaftThreading.cpp ../components/core/DeviceTypes/DeviceTypeRecords.cpp

# Output binaries
MAIN_OUTPUT = linux_unit_tests
DEVICE_TYPE_OUTPUT = device_type_tests

all: $(MAIN_OUTPUT) $(DEVICE_TYPE_OUTPUT)

$(MAIN_OUTPUT): $(MAIN_SOURCES) ../components/core/RaftJson/RaftJson.h ../components/core/Utils/RaftUtils.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $(MAIN_OUTPUT) $(MAIN_SOURCES)

$(DEVICE_TYPE_OUTPUT): $(DEVICE_TYPE_SOURCES) ../components/core/DeviceTypes/DeviceTypeRecords.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $(DEVICE_TYPE_OUTPUT) $(DEVICE_TYPE_SOURCES)

clean:
	rm -f $(MAIN_OUTPUT) $(DEVICE_TYPE_OUTPUT)
