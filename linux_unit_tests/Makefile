# Compiler
CC = g++

# Compiler flags
CFLAGS = -Wall -std=c++20 -g -DRAFT_CORE

# Include paths
INCLUDES = -I../unit_tests/main \
  -I../components/core/ArduinoUtils \
  -I../components/core/RaftJson \
  -I../components/core/Utils \
  -I../components/core/NamedValueProvider \
  -I../components/core/SupervisorStats \
  -I../components/core/DebugGlobals \
  -I../components/core/SysMod \
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
  -I../components/core/DeviceTypes \
  -I../components/core/Bus \
  -I../components/core/RaftDevice \
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
  ../components/core/FileSystem/FileSystem.cpp

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
