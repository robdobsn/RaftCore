cmake_minimum_required(VERSION 3.16)

set(RAFT_COMMS_REQUIRES 
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
  set(RAFT_COMMS_REQUIRES ${RAFT_COMMS_REQUIRES} esp_psram)
  set(RAFT_COMMS_REQUIRES ${RAFT_COMMS_REQUIRES} esp_adc)
endif()

set(TEST_LITTLEFS_FOLDER ${COMPONENT_DIR}/../esp_littlefs-src)
get_filename_component(TEST_LITTLEFS_SRC ${TEST_LITTLEFS_FOLDER} REALPATH)
if (EXISTS "${TEST_LITTLEFS_SRC}")
  message(STATUS "========================================= LittleFS component found")
  set(RAFT_COMMS_REQUIRES ${RAFT_COMMS_REQUIRES} esp_littlefs-src )
endif()

idf_component_register(
  SRCS
    "comms/CommsChannels/CommsChannel.cpp"
    "comms/CommsChannels/CommsChannelManager.cpp"
    "comms/CommsChannelMsg/CommsChannelMsg.cpp"
    "comms/FileStreamProtocols/FileDownloadOKTOProtocol.cpp"
    "comms/FileStreamProtocols/FileStreamBase.cpp"
    "comms/FileStreamProtocols/FileUploadHTTPProtocol.cpp"
    "comms/FileStreamProtocols/FileUploadOKTOProtocol.cpp"
    "comms/FileStreamProtocols/StreamDatagramProtocol.cpp"
    "comms/ProtocolBase/ProtocolBase.cpp"
    "comms/ProtocolExchange/FileStreamSession.cpp"
    "comms/ProtocolExchange/ProtocolExchange.cpp"
    "comms/ProtocolOverAscii/ProtocolOverAscii.cpp"
    "comms/ProtocolRawMsg/ProtocolRawMsg.cpp"
    "comms/ProtocolRICFrame/ProtocolRICFrame.cpp"
    "comms/ProtocolRICJSON/ProtocolRICJSON.cpp"
    "comms/ProtocolRICSerial/ProtocolRICSerial.cpp"
    "comms/RICRESTMsg/RICRESTMsg.cpp"
    "comms/ROSSerial/ROSSerialMsg.cpp"
    "core/ArduinoUtils/ArduinoGPIO.cpp"
    "core/ArduinoUtils/ArduinoStdlibNonISO.c"
    "core/ArduinoUtils/ArduinoTime.cpp"
    "core/ArduinoUtils/ArduinoWString.cpp"
    "core/ArPreferences/ArPreferences.cpp"
    "core/Bus/BusSerial.cpp"
    "core/Config/ConfigBase.cpp"
    "core/Config/ConfigMulti.cpp"
    "core/Config/ConfigNVS.cpp"
    "core/ConfigPinMap/ConfigPinMap.cpp"
    "core/DataCollection/SampleCollectorJSON.cpp"
    "core/DebounceButton/DebounceButton.cpp"
    "core/DebugGlobals/DebugGlobals.cpp"
    "core/ExpressionEval/ExpressionContext.cpp"
    "core/ExpressionEval/ExpressionEval.cpp"
    "core/ExpressionEval/tinyexpr.c"
    "core/FileSystem/FileSystem.cpp"
    "core/FileSystem/FileSystemChunker.cpp"
    "core/libb64/cencode.cpp"
    "core/Logger/LoggerCore.cpp"
    "core/MiniHDLC/MiniHDLC.cpp"
    "core/MQTT/MQTTProtocol.cpp"
    "core/MQTT/RaftMQTTClient.cpp"
    "core/NetworkSystem/NetworkSystem.cpp"
    "core/NetworkSystem/WiFiScanner.cpp"
    "core/RaftJson/RaftJsmn.cpp"
    "core/RaftJson/RaftJson.cpp"
    "core/RestAPIEndpoints/RestAPIEndpointManager.cpp"
    "core/StatusIndicator/StatusIndicator.cpp"
    "core/SupervisorStats/SupervisorStats.cpp"
    "core/SysManager/SysManager.cpp"
    "core/SysMod/SysModBase.cpp"
    "core/SysTypes/SysTypeManager.cpp"
    "core/Utils/ESPUtils.cpp"
    "core/Utils/RaftUtils.cpp"
  INCLUDE_DIRS
    "comms/CommsBridgeMsg"
    "comms/CommsChannels"
    "comms/CommsChannelMsg"
    "comms/CommsCoreIF"
    "comms/FileStreamProtocols"
    "comms/ProtocolBase"
    "comms/ProtocolExchange"
    "comms/ProtocolOverAscii"
    "comms/ProtocolRawMsg"
    "comms/ProtocolRICFrame"
    "comms/ProtocolRICJSON"
    "comms/ProtocolRICSerial"
    "comms/RICRESTMsg"
    "comms/ROSSerial"
    "core/APICommon"
    "core/ArduinoUtils"
    "core/ArPreferences"
    "core/Bus"
    "core/Config"
    "core/ConfigPinMap"
    "core/DataCollection"
    "core/DebounceButton"
    "core/DebugGlobals"
    "core/ExpressionEval"
    "core/FileSystem"
    "core/libb64"
    "core/Logger"
    "core/MiniHDLC"
    "core/MQTT"
    "core/NamedValueProvider"
    "core/NetworkSystem"
    "core/NumericalFilters"
    "core/RaftJson"
    "core/RestAPIEndpoints"
    "core/RingBuffer"
    "core/StatusIndicator"
    "core/SupervisorStats"
    "core/SysManager"
    "core/SysMod"
    "core/SysTypes"
    "core/ThreadSafeQueue"
    "core/Utils"
  REQUIRES
    ${RAFT_COMMS_REQUIRES}
)
