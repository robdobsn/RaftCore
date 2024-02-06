# RaftCore build system
# Rob Dobson 2024

# Ensure CMake supports FetchContent
cmake_minimum_required(VERSION 3.16)
include(FetchContent)

# Get build configuration folder
get_filename_component(_build_config_name ${CMAKE_BINARY_DIR} NAME)

# Debug
message (STATUS "------------------ RaftCore BuildConfig ${_build_config_name} ------------------")

# Set the base project name
set(PROJECT_BASENAME "${_build_config_name}")

# Check config dir exists
set(BUILD_CONFIG_DIR "${CMAKE_SOURCE_DIR}/systypes/${_build_config_name}")
if((NOT EXISTS ${BUILD_CONFIG_DIR}) OR (NOT IS_DIRECTORY ${BUILD_CONFIG_DIR}))
    message(FATAL_ERROR "Config directory ${BUILD_CONFIG_DIR} not found.")
endif()

# Use sdkconfig for the selected build configuration
set(SDKCONFIG_DEFAULTS "${BUILD_CONFIG_DIR}/sdkconfig.defaults")
set(SDKCONFIG "${BUILD_CONFIG_DIR}/sdkconfig")

# Check if the sdkconfig file is older than the sdkconfig.defaults file and delete it if so
if(EXISTS ${SDKCONFIG} AND EXISTS ${SDKCONFIG_DEFAULTS})
    if(${SDKCONFIG_DEFAULTS} IS_NEWER_THAN ${SDKCONFIG})
        message(STATUS "------------------ Deleting sdkconfig as sdkconfig.defaults CHANGED ------------------")
        file(REMOVE ${SDKCONFIG})
    else()
        message(STATUS "------------------ Not deleting sdkconfig as sdkconfig.defaults NOT_CHANGED ------------------")
    endif()
else()
    message(STATUS "------------------ Not deleting sdkconfig as NOT_FOUND ------------------")
endif()

# Dependency
set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} ${BUILD_CONFIG_DIR}/sdkconfig.defaults)
set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} ${BUILD_CONFIG_DIR}/sdkconfig)

# Configure build config specific features (options, flags, etc).
include(${BUILD_CONFIG_DIR}/features.cmake)

# System naming
add_compile_definitions(PROJECT_BASENAME="${PROJECT_BASENAME}")

# Set CONFIG_IDF_TARGET from IDF_TARGET if not already set
if(NOT DEFINED CONFIG_IDF_TARGET)
    set(CONFIG_IDF_TARGET ${IDF_TARGET})
endif()

# Include ESP-IDF build system (must be done after setting CONFIG_IDF_TARGET)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set the firmware image name (if not already set)
if(NOT DEFINED FW_IMAGE_NAME)
    set(FW_IMAGE_NAME "${_build_config_name}FW")
endif()

# Configuration message
message(STATUS "------------------ Configuring ${_build_config_name} firmware image name ${FW_IMAGE_NAME} ------------------")

# Set the build artefacts directory
set(RAFT_BUILD_ARTEFACTS_FOLDER "${CMAKE_SOURCE_DIR}/build_raft_artefacts")
file(MAKE_DIRECTORY ${RAFT_BUILD_ARTEFACTS_FOLDER})

# Copy the partitions.csv file from the specific systypes folder to the build artefacts directory
# It should not go into the build_config_dir as the sdkconfig.defaults file must reference a fixed folder
set(_partitions_csv_file "${RAFT_BUILD_ARTEFACTS_FOLDER}/partitions.csv")
message(STATUS "Copying ${BUILD_CONFIG_DIR}/partitions.csv to ${_partitions_csv_file}")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy "${BUILD_CONFIG_DIR}/partitions.csv" ${_partitions_csv_file}
)

# Dependency on partitions.csv
set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} ${_partitions_csv_file})

# List of dependencies of main project
set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} "raftcore")
set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} ${raftcore_SOURCE_DIR})

# Iterate over list of raft components
foreach(_raft_component ${RAFT_COMPONENTS})

    # Split the component name into the component name and the optional tag on the @ symbol
    string(REPLACE "@" ";" _raft_component_split ${_raft_component})
    list(GET _raft_component_split 0 _raft_component_name)
    string(TOLOWER ${_raft_component_name} _raft_component_lower)

    # Get the tag if present
    list(LENGTH _raft_component_split _raft_component_split_len)
    if(${_raft_component_split_len} GREATER 1)
        list(GET _raft_component_split 1 _raft_component_tag)
    endif()

    # Fetch the Raft library
    FetchContent_Declare(
        ${_raft_component_lower}
        GIT_REPOSITORY https://github.com/robdobsn/${_raft_component_name}.git
        GIT_TAG        ${_raft_component_tag}
    )
    FetchContent_Populate(${_raft_component_lower})

    # Add the component dir to the list of extra component dirs
    set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} ${${_raft_component_lower}_SOURCE_DIR})

    # Add the component to the list of dependencies
    set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} ${_raft_component_lower})

endforeach()

# Make sure the SysTypes header is generated before compiling this component (main) if needed
set(_systypes_h "${RAFT_BUILD_ARTEFACTS_FOLDER}/SysTypeInfoRecs.h")
set(_systypes_json "${BUILD_CONFIG_DIR}/SysTypes.json")
set(_systypes_template "${raftcore_SOURCE_DIR}/components/core/SysTypes/SysTypeInfoRecs.cpp.template")
message(STATUS "------------------ Generating SysTypeInfoRecs.h ------------------")
message(STATUS "Generating ${_systypes_cpp} from file ${_systypes_json}")
execute_process(
    COMMAND python3 ${raftcore_SOURCE_DIR}/scripts/GenerateSysTypes.py ${_systypes_json} ${_systypes_h} --cpp_template ${_systypes_template}
)

# Dependency on SysTypes header
set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} ${_systypes_h})

# Copy the FS image source folder to the build artefacts directory
set(_full_fs_source_image_path "${BUILD_CONFIG_DIR}/${FS_IMAGE_PATH}")
set(_full_fs_dest_image_path "${RAFT_BUILD_ARTEFACTS_FOLDER}/FSImage")
message(STATUS "------------------ Copying FS Image ------------------")
message(STATUS "Copying ${_full_fs_source_image_path} to ${_full_fs_dest_image_path}")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${_full_fs_dest_image_path}"
)
execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_full_fs_source_image_path}" "${_full_fs_dest_image_path}"
)
message(STATUS "Removing ${_full_fs_dest_image_path}/placeholder")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E remove "${_full_fs_dest_image_path}/placeholder"
)

# Check if UI_SOURCE_PATH is defined
if(DEFINED UI_SOURCE_PATH)
    # Process WebUI files into the dest FS image folder
    set(_full_web_ui_source_path "${BUILD_CONFIG_DIR}/${UI_SOURCE_PATH}")
    message(STATUS "------------------ Generating WebUI ------------------")
    message(STATUS "Generating WebUI from ${_full_web_ui_source_path} to ${_full_fs_dest_image_path}")
    execute_process(
        COMMAND python3 ${raftcore_SOURCE_DIR}/scripts/GenWebUI.py ${WEB_UI_GEN_FLAGS} ${_full_web_ui_source_path} ${_full_fs_dest_image_path}
    )
endif()

# Add optional component folders
set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} ${OPTIONAL_COMPONENTS})

# This makes it easy for VS Code to access the compile commands for the most
# recently built FW revision.
add_custom_target(updateSharedCompileCommands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/compile_commands.json" "${CMAKE_BINARY_DIR}/.."
    DEPENDS "${CMAKE_BINARY_DIR}/compile_commands.json"
    COMMENT "Updating shared compile_commands.json"
)
