# Ensure CMake supports FetchContent
cmake_minimum_required(VERSION 3.16)

set(RAFT_CORE_REQUIRES 
  nvs_flash
  driver
  vfs
  spiffs
  fatfs
  sdmmc
  esp_wifi
  esp_eth
  esp_timer
)

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.0")
  set(RAFT_CORE_REQUIRES ${RAFT_CORE_REQUIRES} esp_psram)
endif()

set(TEST_LITTLEFS_FOLDER ${COMPONENT_DIR}/../esp_littlefs-src)
get_filename_component(TEST_LITTLEFS_SRC ${TEST_LITTLEFS_FOLDER} REALPATH)
if (EXISTS "${TEST_LITTLEFS_SRC}")
  message(STATUS "============================= LittleFS component found")
  set(RAFT_CORE_REQUIRES ${RAFT_CORE_REQUIRES} esp_littlefs-src )
endif()

idf_component_register(
  SRCS
    "components/core/ArduinoUtils/ArduinoGPIO.cpp"
    "components/core/ArduinoUtils/ArduinoStdlibNonISO.c"
    "components/core/ArduinoUtils/ArduinoTime.cpp"
    "components/core/ArduinoUtils/ArduinoWString.cpp"
    "components/core/ArPreferences/ArPreferences.cpp"
    "components/core/Bus/BusSerial.cpp"
    "components/core/CommsChannelManager/CommsChannel.cpp"
    "components/core/CommsChannelManager/CommsChannelManager.cpp"
    "components/core/CommsChannelMsg/CommsChannelMsg.cpp"
    "components/core/Config/ConfigBase.cpp"
    "components/core/Config/ConfigMulti.cpp"
    "components/core/Config/ConfigNVS.cpp"
    "components/core/Config/RdJsmn.cpp"
    "components/core/Config/RdJson.cpp"
    "components/core/ConfigPinMap/ConfigPinMap.cpp"
    "components/core/DebounceButton/DebounceButton.cpp"
    "components/core/ExpressionEval/ExpressionContext.cpp"
    "components/core/ExpressionEval/ExpressionEval.cpp"
    "components/core/ExpressionEval/tinyexpr.c"
    "components/core/FileStreamProtocols/FileDownloadOKTOProtocol.cpp"
    "components/core/FileStreamProtocols/FileStreamBase.cpp"
    "components/core/FileStreamProtocols/FileUploadHTTPProtocol.cpp"
    "components/core/FileStreamProtocols/FileUploadOKTOProtocol.cpp"
    "components/core/FileStreamProtocols/StreamDatagramProtocol.cpp"
    "components/core/FileSystem/FileSystem.cpp"
    "components/core/FileSystem/FileSystemChunker.cpp"
    "components/core/libb64/cencode.cpp"
    "components/core/Logger/LoggerCore.cpp"
    "components/core/MiniHDLC/MiniHDLC.cpp"
    "components/core/MQTT/MQTTProtocol.cpp"
    "components/core/MQTT/RaftMQTTClient.cpp"
    "components/core/NetworkSystem/NetworkSystem.cpp"
    "components/core/NetworkSystem/WiFiScanner.cpp"
    "components/core/ProtocolBase/ProtocolBase.cpp"
    "components/core/ProtocolExchange/FileStreamSession.cpp"
    "components/core/ProtocolExchange/ProtocolExchange.cpp"
    "components/core/ProtocolOverAscii/ProtocolOverAscii.cpp"
    "components/core/ProtocolRawMsg/ProtocolRawMsg.cpp"
    "components/core/ProtocolRICFrame/ProtocolRICFrame.cpp"
    "components/core/ProtocolRICJSON/ProtocolRICJSON.cpp"
    "components/core/ProtocolRICSerial/ProtocolRICSerial.cpp"
    "components/core/RestAPIEndpoints/RestAPIEndpointManager.cpp"
    "components/core/RICRESTMsg/RICRESTMsg.cpp"
    "components/core/ROSSerial/ROSSerialMsg.cpp"
    "components/core/StatusIndicator/StatusIndicator.cpp"
    "components/core/SupervisorStats/SupervisorStats.cpp"
    "components/core/SysManager/SysManager.cpp"
    "components/core/SysMod/SysModBase.cpp"
    "components/core/SysTypes/SysTypeManager.cpp"
    "components/core/Utils/ESPUtils.cpp"
    "components/core/Utils/RaftUtils.cpp"
  INCLUDE_DIRS
    "components/core/APICommon"
    "components/core/ArduinoUtils"
    "components/core/ArPreferences"
    "components/core/Bus"
    "components/core/CommsBridgeMsg"
    "components/core/CommsChannelManager"
    "components/core/CommsChannelMsg"
    "components/core/CommsCoreIF"
    "components/core/Config"
    "components/core/ConfigPinMap"
    "components/core/DebounceButton"
    "components/core/ExpressionEval"
    "components/core/FileStreamProtocols"
    "components/core/FileSystem"
    "components/core/libb64"
    "components/core/Logger"
    "components/core/MiniHDLC"
    "components/core/MQTT"
    "components/core/NetCoreIF"
    "components/core/NetworkSystem"
    "components/core/NumericalFilters"
    "components/core/ProtocolBase"
    "components/core/ProtocolExchange"
    "components/core/ProtocolOverAscii"
    "components/core/ProtocolRawMsg"
    "components/core/ProtocolRICFrame"
    "components/core/ProtocolRICJSON"
    "components/core/ProtocolRICSerial"
    "components/core/RestAPIEndpoints"
    "components/core/RICRESTMsg"
    "components/core/ROSSerial"
    "components/core/RingBuffer"
    "components/core/StatusIndicator"
    "components/core/SupervisorStats"
    "components/core/SysManager"
    "components/core/SysMod"
    "components/core/SysTypes"
    "components/core/ThreadSafeQueue"
    "components/core/Utils"
  REQUIRES
    ${RAFT_CORE_REQUIRES}
)
