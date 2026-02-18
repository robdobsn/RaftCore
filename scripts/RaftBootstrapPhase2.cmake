# RaftCore build system
# Rob Dobson 2025

# Ensure CMake supports FetchContent
cmake_minimum_required(VERSION 3.16)
include(FetchContent)

# Find Python
find_package(Python3 REQUIRED)

################################################
# Show CMake phase
################################################

if (CMAKE_GENERATOR)
    message(STATUS "\n------------------ CMAKE GENERATION PHASE")
else()
    message(STATUS "\n------------------ CMAKE CONFIGURATION PHASE")
endif()

################################################
# SDKConfig
################################################

# Use sdkconfig for the selected build configuration
set(SDKCONFIG_DEFAULTS "${BUILD_CONFIG_DIR}/sdkconfig.defaults")
set(SDKCONFIG "${RAFT_BUILD_ARTIFACTS_FOLDER}/sdkconfig")

# Custom command to change the sdkconfig file based on the sdkconfig.defaults file dependency
add_custom_command(
    OUTPUT ${SDKCONFIG}
    COMMAND ${CMAKE_COMMAND} -E copy ${SDKCONFIG_DEFAULTS} ${SDKCONFIG}
    DEPENDS ${SDKCONFIG_DEFAULTS}
    COMMENT "Copying sdkconfig.defaults to sdkconfig"
)

# Custom target to ensure the sdkconfig file is generated before the main project is built
add_custom_target(
    sdkconfig ALL
    DEPENDS ${SDKCONFIG}
    COMMENT "Copying sdkconfig.defaults to sdkconfig"
)

# Add project dependencies
set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} sdkconfig)

################################################
# SysTypes Header
################################################

# Make sure the SysTypes header is generated before compiling this component (main) if needed
set(_systypes_out "${RAFT_BUILD_ARTIFACTS_FOLDER}/SysTypeInfoRecs.h")
set(_systypes_json "${BUILD_CONFIG_DIR}/SysTypes.json")
set(_systypes_template "${raftcore_SOURCE_DIR}/components/core/SysTypes/SysTypeInfoRecs.cpp.template")
message(STATUS "\n------------------ Generating SysTypeInfoRecs.h")
message(STATUS "Generating ${_systypes_out} from file ${_systypes_json}")
add_custom_command(
    OUTPUT ${_systypes_out}
    COMMAND python3 ${raftcore_SOURCE_DIR}/scripts/GenerateSysTypes.py ${_systypes_json} ${_systypes_out} --cpp_template ${_systypes_template}
    DEPENDS ${_systypes_json} ${_systypes_template}
    WORKING_DIRECTORY ${RAFT_BUILD_ARTIFACTS_FOLDER}
    COMMENT "Generating SysTypeInfoRecs.h"
)
add_custom_target(
    SysTypeInfoRecs ALL
    DEPENDS ${_systypes_out}
    COMMENT "Generating SysTypeInfoRecs.h")

# Dependency on SysTypes header
set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} SysTypeInfoRecs)

# For backwards compatibility set _build_config_name variable to the current _systype_name
set(_build_config_name ${_systype_name})

################################################
# DevTypes Generation
################################################

# Check if DEV_TYPE_JSON_FILES is defined and not empty
if (DEFINED DEV_TYPE_JSON_FILES AND NOT DEV_TYPE_JSON_FILES STREQUAL "")
    # Convert DEV_TYPE_JSON_FILES to a list if it's a single string
    if (NOT DEV_TYPE_JSON_FILES MATCHES ";")
        set(DEV_TYPE_JSON_FILES "${DEV_TYPE_JSON_FILES}")
    endif()

    # Create a new list to store the updated file paths
    set(UPDATED_DEV_TYPE_JSON_FILES "")

    # Iterate over each file in the list
    foreach(FILE_PATH ${DEV_TYPE_JSON_FILES})
        # Check if the file exists
        if (EXISTS "${FILE_PATH}")
            list(APPEND UPDATED_DEV_TYPE_JSON_FILES "${FILE_PATH}")
        else()
            # Prepend ${raftcore_SOURCE_DIR} and check again
            set(PREPENDED_PATH "${raftcore_SOURCE_DIR}/${FILE_PATH}")
            if (EXISTS "${PREPENDED_PATH}")
                list(APPEND UPDATED_DEV_TYPE_JSON_FILES "${PREPENDED_PATH}")
            else()
                message(WARNING "File not found: ${FILE_PATH} (even after prepending ${raftcore_SOURCE_DIR})")
            endif()
        endif()
    endforeach()

    # Update DEV_TYPE_JSON_FILES with the new list
    set(DEV_TYPE_JSON_FILES "${UPDATED_DEV_TYPE_JSON_FILES}")
    message(STATUS "DEV_TYPE_JSON_FILES: ${DEV_TYPE_JSON_FILES}")
endif()

# Device type record paths
set(POSSIBLE_FILES 
    "${CMAKE_SOURCE_DIR}/systypes/Common/DevTypes.json"
    "${BUILD_CONFIG_DIR}/DevTypes.json"
)
foreach(FILE_PATH ${POSSIBLE_FILES})
    if(EXISTS ${FILE_PATH})
        list(APPEND DEV_TYPE_JSON_FILES ${FILE_PATH})
    endif()
endforeach()
# Debugging output
set(DEV_TYPE_RECS_HEADER "${RAFT_BUILD_ARTIFACTS_FOLDER}/DeviceTypeRecords_generated.h")
set(DEV_POLL_RECS_HEADER "${RAFT_BUILD_ARTIFACTS_FOLDER}/DevicePollRecords_generated.h")
# Convert DEV_TYPE_JSON_FILES to a comma-separated string
string(JOIN "," DEV_TYPE_JSON_ARG ${DEV_TYPE_JSON_FILES})
# Custom command to generate device type records header file from JSON
add_custom_command(
    OUTPUT ${DEV_TYPE_RECS_HEADER} ${DEV_POLL_RECS_HEADER}
    COMMAND ${Python3_EXECUTABLE} "${raftcore_SOURCE_DIR}/scripts/ProcessDevTypeJsonToC.py" "[${DEV_TYPE_JSON_ARG}]" "${DEV_TYPE_RECS_HEADER}" "${DEV_POLL_RECS_HEADER}"
    DEPENDS ${DEV_TYPE_JSON_FILES}
    COMMENT "\nGenerating Device Record headers from JSON"
)

# Add custom target for generating device records
add_custom_target(generate_dev_ident_header ALL DEPENDS ${DEV_TYPE_RECS_HEADER} ${DEV_POLL_RECS_HEADER})

set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} generate_dev_ident_header)

################################################
# ESP IDF
################################################

# Set CONFIG_IDF_TARGET from IDF_TARGET if not already set
if(NOT DEFINED CONFIG_IDF_TARGET)
    set(CONFIG_IDF_TARGET ${IDF_TARGET})
endif()

# Include ESP-IDF build system (must be done after setting CONFIG_IDF_TARGET)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set the firmware image name (if not already set)
if(NOT DEFINED FW_IMAGE_NAME)
    set(FW_IMAGE_NAME "${_systype_name}")
endif()

# Configuration message
message(STATUS "------------------ Firmware image ${FW_IMAGE_NAME} ------------------")

# List of dependencies of main project
set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} ${OPTIONAL_COMPONENTS})

################################################
# Partition table
################################################

# Copy the partitions.csv file to a fixed location so all sdkconfig.defaults files can reference the same path
# This avoids the need for systype-specific paths in sdkconfig.defaults
set(_partitions_csv_file "${CMAKE_SOURCE_DIR}/build/raft/partitions.csv")
set(_partitions_csv_dir "${CMAKE_SOURCE_DIR}/build/raft")

# Ensure the directory exists
file(MAKE_DIRECTORY ${_partitions_csv_dir})

# Copy the partitions.csv file during configuration
execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy "${BUILD_CONFIG_DIR}/partitions.csv" ${_partitions_csv_file}
)

# Custom command to copy the partitions.csv file when it changes
add_custom_command(
    OUTPUT ${_partitions_csv_file}
    COMMAND ${CMAKE_COMMAND} -E copy "${BUILD_CONFIG_DIR}/partitions.csv" ${_partitions_csv_file}
    DEPENDS "${BUILD_CONFIG_DIR}/partitions.csv"
    COMMENT "Copying partitions.csv to fixed build location"
)

# Custom target to ensure the partitions.csv file is generated before the main project is built
add_custom_target(
    partitions_csv ALL
    DEPENDS ${_partitions_csv_file}
    COMMENT "Ensuring partitions.csv is in fixed build location"
)

# Dependency on partitions.csv
set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} partitions_csv)

################################################
# WebUI
################################################

# Check if UI_SOURCE_PATH is defined
if(DEFINED UI_SOURCE_PATH)
    # Process WebUI files into a temporary folder
    set(_full_web_ui_source_path "${BUILD_CONFIG_DIR}/${UI_SOURCE_PATH}")

    # Validate the WebUI source directory exists
    if(NOT EXISTS "${_full_web_ui_source_path}")
        message(FATAL_ERROR
            "\n==================== WebUI Build Error ====================\n"
            "UI_SOURCE_PATH is set to '${UI_SOURCE_PATH}' but the resolved\n"
            "directory does not exist:\n"
            "  ${_full_web_ui_source_path}\n\n"
            "Either create this directory with a WebUI project, or comment\n"
            "out the set(UI_SOURCE_PATH ...) line in your features.cmake.\n"
            "============================================================\n"
        )
    endif()
    if(NOT EXISTS "${_full_web_ui_source_path}/package.json")
        message(FATAL_ERROR
            "\n==================== WebUI Build Error ====================\n"
            "UI_SOURCE_PATH resolves to:\n"
            "  ${_full_web_ui_source_path}\n"
            "but this directory does not contain a package.json file.\n"
            "Expected a WebUI project (with npm/package.json) at this path.\n"
            "============================================================\n"
        )
    endif()

    set(_web_ui_build_folder_path "${RAFT_BUILD_ARTIFACTS_FOLDER}/BuildWebUI")
    # Ensure the WebUI build folder exists and is clean
    file(MAKE_DIRECTORY ${_web_ui_build_folder_path})

    # Custom command to generate the WebUI
    file(GLOB_RECURSE WebUI_SOURCES "${_full_web_ui_source_path}/src/*")
    add_custom_command(
        OUTPUT ${_web_ui_build_folder_path}/.built
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${_web_ui_build_folder_path}/*
        COMMAND python3 ${raftcore_SOURCE_DIR}/scripts/GenWebUI.py ${WEB_UI_GEN_FLAGS} ${_full_web_ui_source_path} ${_web_ui_build_folder_path}
        COMMAND ${CMAKE_COMMAND} -E touch ${_web_ui_build_folder_path}/.built
        WORKING_DIRECTORY ${_full_web_ui_source_path}
        DEPENDS ${WebUI_SOURCES}
        COMMENT "Generating WebUI"
    )

    # Add the WebUI generation as a target
    add_custom_target(
        WebUI ALL
        DEPENDS ${_web_ui_build_folder_path}/.built
        COMMENT "Generating WebUI"
    )

else()
    add_custom_target(
        WebUI ALL
        COMMENT "No WebUI defined"
    )
endif()

################################################
# FS Image
################################################

# Copy the FS image source folder to the build artifacts directory
set(_full_fs_source_image_path "${BUILD_CONFIG_DIR}/${FS_IMAGE_PATH}")
set(_full_fs_dest_image_path "${RAFT_BUILD_ARTIFACTS_FOLDER}/FSImage")

# Always copy FS and WebUI files
add_custom_target(AlwaysCopyFSAndWebUI
    COMMAND echo "-------------- Always copy FS and WebUI files ----------------------"
)

add_custom_target(CopyFSAndWebUI ALL
    COMMAND ${CMAKE_COMMAND} -E echo "------------------ Copying FSImage files to file system --------------------"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${_full_fs_dest_image_path}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_full_fs_source_image_path}" "${_full_fs_dest_image_path}"
    COMMAND ${CMAKE_COMMAND} -E remove "${_full_fs_dest_image_path}/placeholder"
    DEPENDS WebUI AlwaysCopyFSAndWebUI
    COMMENT "Copying FSImage files to the FS Image directory"
)

if(DEFINED UI_SOURCE_PATH)
    add_custom_command(TARGET CopyFSAndWebUI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "------------------ Copying Web UI files to file system --------------------"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_web_ui_build_folder_path}" "${_full_fs_dest_image_path}"
        COMMENT "Copying WebUI files to the FS Image directory"
    )
endif()

# Dependency on FS image
set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} CopyFSAndWebUI WebUI)

################################################
# Generate File System Image (Deferred)
################################################

# Defer the file system image generation until after ESP-IDF components are loaded
if(DEFINED FS_TYPE)
    cmake_language(DEFER CALL include "${raftcore_SOURCE_DIR}/scripts/RaftGenFSImage.cmake")
endif()

################################################
# compile_commands.json
################################################

# This makes it easy for VS Code to access the compile commands for the most
# recently built FW revision.
add_custom_target(updateSharedCompileCommands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/compile_commands.json" "${CMAKE_BINARY_DIR}/.."
    DEPENDS "${CMAKE_BINARY_DIR}/compile_commands.json"
    COMMENT "Updating shared compile_commands.json"
)
