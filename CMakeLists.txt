# Ensure CMake supports FetchContent
cmake_minimum_required(VERSION 3.16)

# Set the build artifacts directory
set(RAFT_BUILD_ARTIFACTS_FOLDER "${CMAKE_BINARY_DIR}/raft")
file(MAKE_DIRECTORY ${RAFT_BUILD_ARTIFACTS_FOLDER})

# Set RAFT_CORE_REQUIRES based on IDF version
set(RAFT_CORE_REQUIRES 
  nvs_flash
  driver
  vfs
  esp_wifi
  esp_timer
  esp_app_format
)

set(RAFT_CORE_COMPILE_DEFINITIONS "")

if (DEFINED FS_TYPE AND FS_TYPE STREQUAL "littlefs")
  set(RAFT_CORE_REQUIRES ${RAFT_CORE_REQUIRES} littlefs)
  list(APPEND RAFT_CORE_COMPILE_DEFINITIONS FILE_SYSTEM_SUPPORTS_LITTLEFS)
else()
  set(RAFT_CORE_REQUIRES ${RAFT_CORE_REQUIRES} spiffs)
  list(APPEND RAFT_CORE_COMPILE_DEFINITIONS RAFT_FILESYSTEM_HAS_SPIFFS)
endif()

if (DEFINED RAFT_ENABLE_SD AND RAFT_ENABLE_SD)
  set(RAFT_CORE_REQUIRES ${RAFT_CORE_REQUIRES} fatfs sdmmc)
  list(APPEND RAFT_CORE_COMPILE_DEFINITIONS RAFT_FILESYSTEM_HAS_FATSD)
endif()

# esp_eth is unconditionally required because ESP-IDF resolves component
# requirements before sdkconfig is generated (CONFIG_* vars are unavailable
# during the early expansion phase). Source-level #ifdef guards in
# NetworkSystem.h handle compile-time conditionality.
set(RAFT_CORE_REQUIRES ${RAFT_CORE_REQUIRES} esp_eth)

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.0")
  set(RAFT_CORE_REQUIRES ${RAFT_CORE_REQUIRES} esp_psram)
  set(RAFT_CORE_REQUIRES ${RAFT_CORE_REQUIRES} esp_adc)
endif()

set(RAFT_CORE_REQUIRES ${RAFT_CORE_REQUIRES} mdns)

# ESP-IDF-specific configurations
idf_component_register(
  NAME
    RaftCore
  SRCS
    "components/comms/CommsChannels/CommsChannel.cpp"
    "components/comms/CommsChannels/CommsChannelManager.cpp"
    "components/comms/CommsChannelMsg/CommsChannelMsg.cpp"
    "components/comms/FileStreamProtocols/FileDownloadOKTOProtocol.cpp"
    "components/comms/FileStreamProtocols/FileStreamBase.cpp"
    "components/comms/FileStreamProtocols/FileUploadHTTPProtocol.cpp"
    "components/comms/FileStreamProtocols/FileUploadOKTOProtocol.cpp"
    "components/comms/FileStreamProtocols/StreamDatagramProtocol.cpp"
    "components/comms/ProtocolBase/ProtocolBase.cpp"
    "components/comms/ProtocolExchange/FileStreamSession.cpp"
    "components/comms/ProtocolExchange/ProtocolExchange.cpp"
    "components/comms/ProtocolOverAscii/ProtocolOverAscii.cpp"
    "components/comms/ProtocolRawMsg/ProtocolRawMsg.cpp"
    "components/comms/ProtocolRICFrame/ProtocolRICFrame.cpp"
    "components/comms/ProtocolRICJSON/ProtocolRICJSON.cpp"
    "components/comms/ProtocolRICSerial/ProtocolRICSerial.cpp"
    "components/comms/RICRESTMsg/RICRESTMsg.cpp"
    "components/comms/ROSSerial/ROSSerialMsg.cpp"
    "components/core/ArduinoUtils/ArduinoGPIO.cpp"
    "components/core/ArduinoUtils/ArduinoStdlibNonISO.c"
    "components/core/ArduinoUtils/ArduinoTime.cpp"
    "components/core/ArduinoUtils/ArduinoWString.cpp"
    "components/core/ArPreferences/ArPreferences.cpp"
    "components/core/Bus/BusAddrRecord.cpp"
    "components/core/Bus/BusAddrStatus.cpp"
    "components/core/Bus/BusSerial.cpp"
    "components/core/Bus/DeviceStatus.cpp"
    "components/core/Bus/RaftBusSystem.cpp"
    "components/core/ConfigPinMap/ConfigPinMap.cpp"
    "components/core/DebounceButton/DebounceButton.cpp"
    "components/core/DebugGlobals/DebugGlobals.cpp"
    "components/core/DeviceManager/DeviceFactory.cpp"
    "components/core/DeviceManager/DeviceManager.cpp"
    "components/core/DeviceManager/DemoDevice.cpp"
    "components/core/DeviceTypes/DeviceTypeRecords.cpp"
    "components/core/DNSResolver/DNSResolver.cpp"
    "components/core/ExpressionEval/ExpressionContext.cpp"
    "components/core/ExpressionEval/ExpressionEval.cpp"
    "components/core/ExpressionEval/tinyexpr.c"
    "components/core/FileSystem/FileSystem.cpp"
    "components/core/FileSystem/FileSystemChunker.cpp"
    "components/core/LEDPixels/ESP32RMTLedStrip.cpp"
    "components/core/LEDPixels/LEDPixels.cpp"
    "components/core/LEDPixels/LEDStripEncoder.c"
    "components/core/LEDPixels/LEDPixelsDevice.cpp"
    "components/core/libb64/cencode.cpp"
    "components/core/Logger/LoggerCore.cpp"
    "components/core/MiniHDLC/MiniHDLC.cpp"
    "components/core/MQTT/MQTTProtocol.cpp"
    "components/core/MQTT/RaftMQTTClient.cpp"
    "components/core/NamedValueProvider/NamedValueProvider.cpp"
    "components/core/NetworkSystem/NetworkSystem.cpp"
    "components/core/NetworkSystem/WiFiScanner.cpp"
    "components/core/RaftCoreApp/RaftCoreApp.cpp"
    "components/core/RaftDevice/RaftDevice.cpp"
    "components/core/RaftJson/RaftJsonNVS.cpp"
    "components/core/RestAPIEndpoints/RestAPIEndpointManager.cpp"
    "components/core/StatusIndicator/StatusIndicator.cpp"
    "components/core/SupervisorStats/SupervisorStats.cpp"
    "components/core/SysManager/SysManager.cpp"
    "components/core/SysMod/RaftSysMod.cpp"
    "components/core/SysTypes/SysTypeManager.cpp"
    "components/core/Utils/PlatformUtils.cpp"
    "components/core/Utils/RaftThreading.cpp"
    "components/core/Utils/RaftUtils.cpp"
    ${RAFT_CORE_ADDITIONAL_SRCS}
  INCLUDE_DIRS
    "components"
    "components/comms/CommsBridgeMsg"
    "components/comms/CommsChannels"
    "components/comms/CommsChannelMsg"
    "components/comms/CommsCoreIF"
    "components/comms/FileStreamProtocols"
    "components/comms/ProtocolBase"
    "components/comms/ProtocolExchange"
    "components/comms/ProtocolOverAscii"
    "components/comms/ProtocolRawMsg"
    "components/comms/ProtocolRICFrame"
    "components/comms/ProtocolRICJSON"
    "components/comms/ProtocolRICSerial"
    "components/comms/RICRESTMsg"
    "components/comms/ROSSerial"
    "components/core/APICommon"
    "components/core/ArduinoUtils"
    "components/core/ArPreferences"
    "components/core/Bus"
    "components/core/ConfigPinMap"
    "components/core/DebounceButton"
    "components/core/DebugGlobals"
    "components/core/DeviceManager"
    "components/core/DeviceTypes"
    "components/core/DNSResolver"
    "components/core/ExpressionEval"
    "components/core/FileSystem"
    "components/core/LEDPixels"
    "components/core/libb64"
    "components/core/Logger"
    "components/core/MiniHDLC"
    "components/core/MQTT"
    "components/core/NamedValueProvider"
    "components/core/NetworkSystem"
    "components/core/NumericalFilters"
    "components/core/RaftCoreApp"
    "components/core/RaftDevice"
    "components/core/RaftJson"
    "components/core/RestAPIEndpoints"
    "components/core/RingBuffer"
    "components/core/StatusIndicator"
    "components/core/SupervisorStats"
    "components/core/SysManager"
    "components/core/SysMod"
    "components/core/SysTypes"
    "components/core/ThreadSafeQueue"
    "components/core/Utils"
    ${RAFT_CORE_ADDITIONAL_INCLUDES}
    ${RAFT_BUILD_ARTIFACTS_FOLDER}
  REQUIRES
    ${RAFT_CORE_REQUIRES}
)

if(NOT CMAKE_SCRIPT_MODE_FILE)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE ${RAFT_CORE_COMPILE_DEFINITIONS})
endif()

# Enable NetBIOS name service (NBSTAT) responses so IP scanners can discover device hostnames
idf_build_set_property(COMPILE_DEFINITIONS "LWIP_NETBIOS_RESPOND_NAME_QUERY=1" APPEND)

