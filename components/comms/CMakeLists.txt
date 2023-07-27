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
    "CommsChannels/CommsChannel.cpp"
    "CommsChannels/CommsChannelManager.cpp"
    "CommsChannelMsg/CommsChannelMsg.cpp"
    "FileStreamProtocols/FileDownloadOKTOProtocol.cpp"
    "FileStreamProtocols/FileStreamBase.cpp"
    "FileStreamProtocols/FileUploadHTTPProtocol.cpp"
    "FileStreamProtocols/FileUploadOKTOProtocol.cpp"
    "FileStreamProtocols/StreamDatagramProtocol.cpp"
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
  INCLUDE_DIRS
    "CommsBridgeMsg"
    "CommsChannels"
    "CommsChannelMsg"
    "CommsCoreIF"
    "FileStreamProtocols"
    "ProtocolBase"
    "ProtocolExchange"
    "ProtocolOverAscii"
    "ProtocolRawMsg"
    "ProtocolRICFrame"
    "ProtocolRICJSON"
    "ProtocolRICSerial"
    "RestAPIEndpoints"
    "RICRESTMsg"
    "ROSSerial"
  REQUIRES
    ${RAFT_COMMS_REQUIRES}
)
