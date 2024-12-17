# Ensure CMake supports FetchContent
cmake_minimum_required(VERSION 3.16)

# Define the project
project(RaftCore VERSION 1.0.0 LANGUAGES C CXX)

# Set source files
set(RAFT_CORE_SOURCE_FILES
    components/comms/CommsChannels/CommsChannel.cpp
    components/comms/CommsChannels/CommsChannelManager.cpp
    components/comms/CommsChannelMsg/CommsChannelMsg.cpp
    components/comms/FileStreamProtocols/FileDownloadOKTOProtocol.cpp
    components/comms/FileStreamProtocols/FileStreamBase.cpp
    components/comms/FileStreamProtocols/FileUploadHTTPProtocol.cpp
    components/comms/FileStreamProtocols/FileUploadOKTOProtocol.cpp
    components/comms/FileStreamProtocols/StreamDatagramProtocol.cpp
    components/comms/ProtocolBase/ProtocolBase.cpp
    components/comms/ProtocolExchange/FileStreamSession.cpp
    components/comms/ProtocolExchange/ProtocolExchange.cpp
    components/comms/ProtocolOverAscii/ProtocolOverAscii.cpp
    components/comms/ProtocolRawMsg/ProtocolRawMsg.cpp
    components/comms/ProtocolRICFrame/ProtocolRICFrame.cpp
    components/comms/ProtocolRICJSON/ProtocolRICJSON.cpp
    components/comms/ProtocolRICSerial/ProtocolRICSerial.cpp
    components/comms/RICRESTMsg/RICRESTMsg.cpp
    components/comms/ROSSerial/ROSSerialMsg.cpp
    components/core/ArduinoUtils/ArduinoGPIO.cpp
    components/core/ArduinoUtils/ArduinoStdlibNonISO.c
    components/core/ArduinoUtils/ArduinoTime.cpp
    components/core/ArduinoUtils/ArduinoWString.cpp
    components/core/Bus/BusAddrStatus.cpp
    components/core/Bus/DeviceStatus.cpp
    components/core/Bus/RaftBusSystem.cpp
    components/core/ConfigPinMap/ConfigPinMap.cpp
    components/core/DebugGlobals/DebugGlobals.cpp
    components/core/DeviceManager/DeviceFactory.cpp
    components/core/DeviceManager/DeviceManager.cpp
    components/core/DeviceTypes/DeviceTypeRecords.cpp
    components/core/ExpressionEval/ExpressionContext.cpp
    components/core/ExpressionEval/ExpressionEval.cpp
    components/core/ExpressionEval/tinyexpr.c
    components/core/libb64/cencode.cpp
    components/core/MiniHDLC/MiniHDLC.cpp
    components/core/MQTT/MQTTProtocol.cpp
    components/core/RaftDevice/RaftDevice.cpp
    components/core/RaftJson/RaftJsonNVS.cpp
    components/core/RestAPIEndpoints/RestAPIEndpointManager.cpp
    components/core/StatusIndicator/StatusIndicator.cpp
    components/core/SupervisorStats/SupervisorStats.cpp
    components/core/SysManager/SysManager.cpp
    components/core/SysMod/RaftSysMod.cpp
    components/core/SysTypes/SysTypeManager.cpp
    components/core/Utils/PlatformUtils.cpp
    components/core/Utils/RaftThreading.cpp
    components/core/Utils/RaftUtils.cpp
)

# Set include directories
set(RAFT_CORE_INCLUDE_DIRS
    ./
    comms/CommsBridgeMsg
    comms/CommsChannels
    comms/CommsChannelMsg
    comms/CommsCoreIF
    comms/FileStreamProtocols
    comms/ProtocolBase
    comms/ProtocolExchange
    comms/ProtocolOverAscii
    comms/ProtocolRawMsg
    comms/ProtocolRICFrame
    comms/ProtocolRICJSON
    comms/ProtocolRICSerial
    comms/RICRESTMsg
    comms/ROSSerial
    core/APICommon
    core/ArduinoUtils
    core/Bus
    core/ConfigPinMap
    core/DebounceButton
    core/DebugGlobals
    core/DeviceManager
    core/DeviceTypes
    core/ExpressionEval
    core/FileSystem
    core/libb64
    core/Logger
    core/MiniHDLC
    core/MQTT
    core/NamedValueProvider
    core/NetworkSystem
    core/NumericalFilters
    core/RaftDevice
    core/RaftJson
    core/RestAPIEndpoints
    core/RingBuffer
    core/StatusIndicator
    core/SupervisorStats
    core/SysManager
    core/SysMod
    core/SysTypes
    core/ThreadSafeQueue
    core/Utils
)

# Define the base directory relative to the current location
file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../.." BASE_DIR)

# message(STATUS "============================ Base directory: ${BASE_DIR}")

# Function to prepend a path prefix to a list of files
function(prepend_path_prefix prefix files dest_var)
    set(processed_files "")
    foreach(file IN LISTS ${files})
        # Normalize the path by removing leading "./"
        string(REPLACE "./" "" file "${file}")
        string(REGEX REPLACE "/$" "" file "${file}")       # Remove trailing "/"
        # Check if the file is already absolute or starts with '/'
        if(NOT IS_ABSOLUTE "${file}" AND NOT file MATCHES "^/")
            list(APPEND processed_files "${prefix}/${file}")
        else()
            list(APPEND processed_files "${file}") # Keep the existing path
        endif()
    endforeach()
    set(${dest_var} "${processed_files}" PARENT_SCOPE)
endfunction()

# Prepend the base path to the source files and include directories
prepend_path_prefix(${BASE_DIR} RAFT_CORE_SOURCE_FILES PREFIXED_SOURCE_FILES)
prepend_path_prefix(${BASE_DIR}/components RAFT_CORE_INCLUDE_DIRS PREFIXED_INCLUDE_DIRS)

# Debugging: Print the processed paths
# message(STATUS "Source files after prefixing: ${PREFIXED_SOURCE_FILES}")
# message(STATUS "Include directories after prefixing: ${PREFIXED_INCLUDE_DIRS}")

# Set the build artifacts directory
set(RAFT_BUILD_ARTIFACTS_FOLDER "${CMAKE_BINARY_DIR}/build_raft_artifacts")
file(MAKE_DIRECTORY ${RAFT_BUILD_ARTIFACTS_FOLDER})

# Find Python
find_package(Python3 REQUIRED)

# Device type record paths
set(JSON_FILE "${BASE_DIR}/devtypes/DeviceTypeRecords.json")
set(DEV_TYPE_RECS_HEADER "${RAFT_BUILD_ARTIFACTS_FOLDER}/DeviceTypeRecords_generated.h")
set(DEV_POLL_RECS_HEADER "${RAFT_BUILD_ARTIFACTS_FOLDER}/DevicePollRecords_generated.h")

# Custom command to generate device type records header file from JSON
add_custom_command(
    OUTPUT ${DEV_TYPE_RECS_HEADER} ${DEV_POLL_RECS_HEADER}
    COMMAND ${Python3_EXECUTABLE} "${BASE_DIR}/scripts/ProcessDevTypeJsonToC.py" "${JSON_FILE}" "${DEV_TYPE_RECS_HEADER}" "${DEV_POLL_RECS_HEADER}"
    DEPENDS ${JSON_FILE}
    COMMENT "---------------------- Generating Device Record headers from JSON ---------------------------"
)

# Add custom target for generating device records
add_custom_target(generate_dev_ident_header DEPENDS ${DEV_TYPE_RECS_HEADER} ${DEV_POLL_RECS_HEADER})

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define the interface target for include paths
add_library(RaftCoreIncludes INTERFACE)
target_include_directories(RaftCoreIncludes INTERFACE
    $<BUILD_INTERFACE:${PREFIXED_INCLUDE_DIRS}>
    $<BUILD_INTERFACE:${RAFT_BUILD_ARTIFACTS_FOLDER}>
)

foreach(dir IN LISTS RAFT_CORE_INCLUDE_DIRS)
target_include_directories(RaftCoreIncludes INTERFACE
    $<INSTALL_INTERFACE:include/RaftCore/${dir}>
)
endforeach()

# Add -fPIC for static library compilation
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# message(STATUS "-================================ PREFIXED_INCLUDE_DIRS: ${PREFIXED_INCLUDE_DIRS}")

# Define the main RaftCore library
add_library(RaftCore STATIC ${PREFIXED_SOURCE_FILES})
target_include_directories(RaftCore PUBLIC 
    ${PREFIXED_INCLUDE_DIRS}
    # ${RAFT_BUILD_ARTIFACTS_FOLDER}
)
target_link_libraries(RaftCore PUBLIC RaftCoreIncludes)

# Set up installation paths
install(TARGETS RaftCore RaftCoreIncludes
    EXPORT RaftCoreTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include/RaftCore
)

# Install include files
install(DIRECTORY ../../components/
    DESTINATION include/RaftCore
    FILES_MATCHING PATTERN "*.h"
)

# Install generated headers
install(DIRECTORY ${RAFT_BUILD_ARTIFACTS_FOLDER}/
DESTINATION include/RaftCore
)

# Export the project configuration for find_package()
install(EXPORT RaftCoreTargets
    FILE RaftCoreConfig.cmake
    NAMESPACE RaftCore::
    DESTINATION lib/cmake/RaftCore
)

# Create a ConfigVersion file for compatibility
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/RaftCoreConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/RaftCoreConfigVersion.cmake"
    DESTINATION lib/cmake/RaftCore
)

# Add the target to the build system
export(TARGETS RaftCore RaftCoreIncludes
    NAMESPACE RaftCore::
    FILE "${CMAKE_CURRENT_BINARY_DIR}/RaftCoreTargets.cmake"
)

# Add dependencies
add_dependencies(RaftCore generate_dev_ident_header)
