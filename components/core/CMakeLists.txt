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
  set(RAFT_CORE_REQUIRES ${RAFT_CORE_REQUIRES} esp_adc)
endif()

set(TEST_LITTLEFS_FOLDER ${COMPONENT_DIR}/../esp_littlefs-src)
get_filename_component(TEST_LITTLEFS_SRC ${TEST_LITTLEFS_FOLDER} REALPATH)
if (EXISTS "${TEST_LITTLEFS_SRC}")
  message(STATUS "========================================= LittleFS component found")
  set(RAFT_CORE_REQUIRES ${RAFT_CORE_REQUIRES} esp_littlefs-src )
endif()

idf_component_register(
  SRCS
    "ArduinoUtils/ArduinoGPIO.cpp"
    "ArduinoUtils/ArduinoStdlibNonISO.c"
    "ArduinoUtils/ArduinoTime.cpp"
    "ArduinoUtils/ArduinoWString.cpp"
    "ArPreferences/ArPreferences.cpp"
    "Bus/BusSerial.cpp"
    "CommsChannelManager/CommsChannel.cpp"
    "CommsChannelManager/CommsChannelManager.cpp"
    "CommsChannelMsg/CommsChannelMsg.cpp"
    "Config/ConfigBase.cpp"
    "Config/ConfigMulti.cpp"
    "Config/ConfigNVS.cpp"
    "Config/RdJsmn.cpp"
    "Config/RdJson.cpp"
    "ConfigPinMap/ConfigPinMap.cpp"
    "DebounceButton/DebounceButton.cpp"
    "ExpressionEval/ExpressionContext.cpp"
    "ExpressionEval/ExpressionEval.cpp"
    "ExpressionEval/tinyexpr.c"
    "FileStreamProtocols/FileDownloadOKTOProtocol.cpp"
    "FileStreamProtocols/FileStreamBase.cpp"
    "FileStreamProtocols/FileUploadHTTPProtocol.cpp"
    "FileStreamProtocols/FileUploadOKTOProtocol.cpp"
    "FileStreamProtocols/StreamDatagramProtocol.cpp"
    "FileSystem/FileSystem.cpp"
    "FileSystem/FileSystemChunker.cpp"
    "LEDPixels/LEDPixels.cpp"
    "LEDPixels/ESP32RMTLedStrip.cpp"
    "LEDPixels/LEDStripEncoder.c"
    "libb64/cencode.cpp"
    "Logger/LoggerCore.cpp"
    "MiniHDLC/MiniHDLC.cpp"
    "MQTT/MQTTProtocol.cpp"
    "MQTT/RaftMQTTClient.cpp"
    "NetworkSystem/NetworkSystem.cpp"
    "NetworkSystem/WiFiScanner.cpp"
    "ProtocolBase/ProtocolBase.cpp"
    "ProtocolExchange/FileStreamSession.cpp"
    "ProtocolExchange/ProtocolExchange.cpp"
    "ProtocolOverAscii/ProtocolOverAscii.cpp"
    "ProtocolRawMsg/ProtocolRawMsg.cpp"
    "ProtocolRICFrame/ProtocolRICFrame.cpp"
    "ProtocolRICJSON/ProtocolRICJSON.cpp"
    "ProtocolRICSerial/ProtocolRICSerial.cpp"
    "RestAPIEndpoints/RestAPIEndpointManager.cpp"
    "RICRESTMsg/RICRESTMsg.cpp"
    "ROSSerial/ROSSerialMsg.cpp"
    "StatusIndicator/StatusIndicator.cpp"
    "SupervisorStats/SupervisorStats.cpp"
    "SysManager/SysManager.cpp"
    "SysMod/SysModBase.cpp"
    "SysTypes/SysTypeManager.cpp"
    "Utils/ESPUtils.cpp"
    "Utils/RaftUtils.cpp"
  INCLUDE_DIRS
    "APICommon"
    "ArduinoUtils"
    "ArPreferences"
    "Bus"
    "CommsBridgeMsg"
    "CommsChannelManager"
    "CommsChannelMsg"
    "CommsCoreIF"
    "Config"
    "ConfigPinMap"
    "DebounceButton"
    "ExpressionEval"
    "FileStreamProtocols"
    "FileSystem"
    "LEDPixels"
    "libb64"
    "Logger"
    "MiniHDLC"
    "MQTT"
    "NamedValueProvider"
    "NetCoreIF"
    "NetworkSystem"
    "NumericalFilters"
    "ProtocolBase"
    "ProtocolExchange"
    "ProtocolOverAscii"
    "ProtocolRawMsg"
    "ProtocolRICFrame"
    "ProtocolRICJSON"
    "ProtocolRICSerial"
    "RestAPIEndpoints"
    "RICRESTMsg"
    "RingBuffer"
    "ROSSerial"
    "StatusIndicator"
    "SupervisorStats"
    "SysManager"
    "SysMod"
    "SysTypes"
    "ThreadSafeQueue"
    "Utils"
  REQUIRES
    ${RAFT_CORE_REQUIRES}
)
