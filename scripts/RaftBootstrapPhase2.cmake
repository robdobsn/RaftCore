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
# Common config directory (base layer for chaining)
################################################

set(_common_config_dir "${CMAKE_SOURCE_DIR}/systypes/Common")

################################################
# SDKConfig (Common + SysType-specific chaining)
################################################

# Merge sdkconfig.defaults: Common as base, systype-specific overrides on top
# ESP-IDF natively supports SDKCONFIG_DEFAULTS as a semicolon-separated list
# where later files override earlier ones
set(_common_sdkconfig "${_common_config_dir}/sdkconfig.defaults")
set(_systype_sdkconfig "${BUILD_CONFIG_DIR}/sdkconfig.defaults")
set(_merged_sdkconfig "${RAFT_BUILD_ARTIFACTS_FOLDER}/sdkconfig.defaults.merged")

# Build the SDKCONFIG_DEFAULTS list for ESP-IDF (Common first, systype second)
set(SDKCONFIG_DEFAULTS "")
if(EXISTS "${_common_sdkconfig}")
    list(APPEND SDKCONFIG_DEFAULTS "${_common_sdkconfig}")
    message(STATUS "SDKConfig: Using Common base from ${_common_sdkconfig}")
endif()
if(EXISTS "${_systype_sdkconfig}")
    list(APPEND SDKCONFIG_DEFAULTS "${_systype_sdkconfig}")
    message(STATUS "SDKConfig: Applying SysType overrides from ${_systype_sdkconfig}")
endif()

# Also generate a merged sdkconfig.defaults for reference and for the sdkconfig seed
set(_merge_sdkconfig_common_arg "")
if(EXISTS "${_common_sdkconfig}")
    set(_merge_sdkconfig_common_arg "--common" "${_common_sdkconfig}")
endif()
set(_merge_sdkconfig_systype_arg "")
if(EXISTS "${_systype_sdkconfig}")
    set(_merge_sdkconfig_systype_arg "--systype" "${_systype_sdkconfig}")
endif()

# Generate merged sdkconfig.defaults at configure time
execute_process(
    COMMAND ${Python3_EXECUTABLE} "${raftcore_SOURCE_DIR}/scripts/MergeSysTypeConfigs.py"
            --mode sdkconfig ${_merge_sdkconfig_common_arg} ${_merge_sdkconfig_systype_arg}
            --output "${_merged_sdkconfig}"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set(SDKCONFIG "${RAFT_BUILD_ARTIFACTS_FOLDER}/sdkconfig")

# Collect dependency files for the sdkconfig custom command
set(_sdkconfig_depends "")
if(EXISTS "${_common_sdkconfig}")
    list(APPEND _sdkconfig_depends "${_common_sdkconfig}")
endif()
if(EXISTS "${_systype_sdkconfig}")
    list(APPEND _sdkconfig_depends "${_systype_sdkconfig}")
endif()

# Custom command to regenerate merged sdkconfig when sources change
add_custom_command(
    OUTPUT ${SDKCONFIG}
    COMMAND ${Python3_EXECUTABLE} "${raftcore_SOURCE_DIR}/scripts/MergeSysTypeConfigs.py"
            --mode sdkconfig ${_merge_sdkconfig_common_arg} ${_merge_sdkconfig_systype_arg}
            --output "${_merged_sdkconfig}"
    COMMAND ${CMAKE_COMMAND} -E copy "${_merged_sdkconfig}" ${SDKCONFIG}
    DEPENDS ${_sdkconfig_depends}
    COMMENT "Merging sdkconfig.defaults (Common + SysType) -> sdkconfig"
)

# Custom target to ensure the sdkconfig file is generated before the main project is built
add_custom_target(
    sdkconfig ALL
    DEPENDS ${SDKCONFIG}
    COMMENT "Ensuring merged sdkconfig is up to date"
)

# Add project dependencies
set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} sdkconfig)

################################################
# SysTypes Header (Common + SysType-specific merging)
################################################

# Merge SysTypes.json: Common as base, systype-specific top-level keys override
set(_common_systypes_json "${_common_config_dir}/SysTypes.json")
set(_systype_systypes_json "${BUILD_CONFIG_DIR}/SysTypes.json")
set(_merged_systypes_json "${RAFT_BUILD_ARTIFACTS_FOLDER}/SysTypes.json.merged")

# Build merge arguments
set(_merge_systypes_common_arg "")
if(EXISTS "${_common_systypes_json}")
    set(_merge_systypes_common_arg "--common" "${_common_systypes_json}")
    message(STATUS "SysTypes: Using Common base from ${_common_systypes_json}")
endif()
set(_merge_systypes_systype_arg "")
if(EXISTS "${_systype_systypes_json}")
    set(_merge_systypes_systype_arg "--systype" "${_systype_systypes_json}")
    message(STATUS "SysTypes: Applying SysType overrides from ${_systype_systypes_json}")
endif()

# Generate merged SysTypes.json at configure time
execute_process(
    COMMAND ${Python3_EXECUTABLE} "${raftcore_SOURCE_DIR}/scripts/MergeSysTypeConfigs.py"
            --mode systypes ${_merge_systypes_common_arg} ${_merge_systypes_systype_arg}
            --output "${_merged_systypes_json}"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Make sure the SysTypes header is generated before compiling this component (main) if needed
set(_systypes_out "${RAFT_BUILD_ARTIFACTS_FOLDER}/SysTypeInfoRecs.h")
set(_systypes_json "${_merged_systypes_json}")
set(_systypes_template "${raftcore_SOURCE_DIR}/components/core/SysTypes/SysTypeInfoRecs.cpp.template")
message(STATUS "\n------------------ Generating SysTypeInfoRecs.h")
message(STATUS "Generating ${_systypes_out} from merged SysTypes.json")

# Collect dependency files for the SysTypes merge
set(_systypes_depends "")
if(EXISTS "${_common_systypes_json}")
    list(APPEND _systypes_depends "${_common_systypes_json}")
endif()
if(EXISTS "${_systype_systypes_json}")
    list(APPEND _systypes_depends "${_systype_systypes_json}")
endif()

add_custom_command(
    OUTPUT ${_systypes_out}
    COMMAND ${Python3_EXECUTABLE} "${raftcore_SOURCE_DIR}/scripts/MergeSysTypeConfigs.py"
            --mode systypes ${_merge_systypes_common_arg} ${_merge_systypes_systype_arg}
            --output "${_merged_systypes_json}"
    COMMAND python3 ${raftcore_SOURCE_DIR}/scripts/GenerateSysTypes.py ${_systypes_json} ${_systypes_out} --cpp_template ${_systypes_template}
    DEPENDS ${_systypes_depends} ${_systypes_template}
    WORKING_DIRECTORY ${RAFT_BUILD_ARTIFACTS_FOLDER}
    COMMENT "Merging SysTypes.json (Common + SysType) and generating SysTypeInfoRecs.h"
)
add_custom_target(
    SysTypeInfoRecs ALL
    DEPENDS ${_systypes_out}
    COMMENT "Generating SysTypeInfoRecs.h from merged SysTypes.json")

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
# Partition table (SysType-specific with Common fallback)
################################################

# Copy the partitions.csv file to a fixed location so all sdkconfig.defaults files can reference the same path
# This avoids the need for systype-specific paths in sdkconfig.defaults
set(_partitions_csv_file "${CMAKE_SOURCE_DIR}/build/raft/partitions.csv")
set(_partitions_csv_dir "${CMAKE_SOURCE_DIR}/build/raft")

# Ensure the directory exists
file(MAKE_DIRECTORY ${_partitions_csv_dir})

# Determine which partitions.csv to use: systype-specific first, then Common fallback
set(_systype_partitions "${BUILD_CONFIG_DIR}/partitions.csv")
set(_common_partitions "${_common_config_dir}/partitions.csv")

if(EXISTS "${_systype_partitions}")
    set(_partitions_source "${_systype_partitions}")
    message(STATUS "Partitions: Using SysType-specific ${_systype_partitions}")
elseif(EXISTS "${_common_partitions}")
    set(_partitions_source "${_common_partitions}")
    message(STATUS "Partitions: Using Common fallback ${_common_partitions}")
else()
    message(FATAL_ERROR "No partitions.csv found in ${BUILD_CONFIG_DIR} or ${_common_config_dir}")
endif()

# Copy the partitions.csv file during configuration
execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy "${_partitions_source}" ${_partitions_csv_file}
)

# Custom command to copy the partitions.csv file when it changes
add_custom_command(
    OUTPUT ${_partitions_csv_file}
    COMMAND ${CMAKE_COMMAND} -E copy "${_partitions_source}" ${_partitions_csv_file}
    DEPENDS "${_partitions_source}"
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
