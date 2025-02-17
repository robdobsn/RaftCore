# RaftBootstrap build system
# Rob Dobson 2025

# Ensure CMake supports FetchContent
cmake_minimum_required(VERSION 3.16)
include(FetchContent)

################################################
# Work out the SysType
################################################

# Get systype folder (either the specified folder or the first folder in the systypes folder)
get_filename_component(_systype_name ${CMAKE_BINARY_DIR} NAME)
get_filename_component(_test_build_base_folder ${CMAKE_BINARY_DIR} DIRECTORY)
get_filename_component(_test_build_base_folder ${_test_build_base_folder} NAME)
if(${_systype_name} STREQUAL "build" AND NOT ${_test_build_base_folder} STREQUAL "build")

    # We are building in the root of the build folder so we now need to find the first systype
    set(_systype_name "")
    file(GLOB _all_systype_dirs RELATIVE ${CMAKE_SOURCE_DIR}/systypes ${CMAKE_SOURCE_DIR}/systypes/*)

    # Iterate over all systype folders (skip the Common folder if it exists)
    foreach(_systype_dir IN LISTS _all_systype_dirs)
        if(IS_DIRECTORY ${CMAKE_SOURCE_DIR}/systypes/${_systype_dir} AND NOT _systype_dir STREQUAL "Common")
            set(_systype_name ${_systype_dir})
            break()
        endif()
    endforeach()

    # Check if a valid systype was found
    if(_systype_name STREQUAL "")
        message(FATAL_ERROR "No valid systype found in systypes folder")
    endif()
else()
    # We are building in a subfolder of the build folder so we can use the subfolder name as the systype name
    if(NOT IS_DIRECTORY ${CMAKE_SOURCE_DIR}/systypes/${_systype_name})
        message(FATAL_ERROR "SysType directory ${CMAKE_SOURCE_DIR}/systypes/${_systype_name} not found.")
    endif()
endif()

# Debug
message (STATUS "\n------------------ Building SysType ${_systype_name} -> ${CMAKE_BINARY_DIR}\n")

# Set the base project name
set(PROJECT_BASENAME "${_systype_name}")
add_compile_definitions(PROJECT_BASENAME="${PROJECT_BASENAME}")

# Check systype dir exists
set(BUILD_CONFIG_DIR "${CMAKE_SOURCE_DIR}/systypes/${_systype_name}")

# Set the build artifacts directory
set(RAFT_BUILD_ARTIFACTS_FOLDER "${CMAKE_BINARY_DIR}/raft")
file(MAKE_DIRECTORY ${RAFT_BUILD_ARTIFACTS_FOLDER})

# Save a file in the build artifacts directory to indicate the systype
file(WRITE "${RAFT_BUILD_ARTIFACTS_FOLDER}/cursystype.txt" "${_systype_name}")

################################################
# Include SysType specific features
################################################

# Configure build config specific features (options, flags, etc).
include(${BUILD_CONFIG_DIR}/features.cmake)

################################################
# Raft components
################################################

# Iterate over list of raft components
foreach(_raft_component ${RAFT_COMPONENTS})

    # Split the component name into the component name and the optional tag on the @ or # symbol
    string(REGEX REPLACE "[@#]" ";" _raft_component_split ${_raft_component})
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
        SOURCE_DIR     ${RAFT_BUILD_ARTIFACTS_FOLDER}/${_raft_component_name}
        GIT_REPOSITORY https://github.com/robdobsn/${_raft_component_name}.git
        GIT_TAG        ${_raft_component_tag}
    )
    FetchContent_Populate(${_raft_component_lower})

    # Add the component dir to the list of extra component dirs
    set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} ${${_raft_component_lower}_SOURCE_DIR})

    # Add the component to the list of dependencies
    set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} ${_raft_component_lower})

endforeach()

################################################
# Continue with the RaftCore script
################################################

# Include the RaftCore script
include("${RAFT_BUILD_ARTIFACTS_FOLDER}/RaftCore/scripts/RaftBootstrapPhase2.cmake")

################################################
# Build the file system image
################################################

if (FS_TYPE STREQUAL "littlefs")
    message(STATUS "Creating LittleFS file system image from ${_full_fs_dest_image_path}")
    littlefs_create_partition_image(fs ${_full_fs_dest_image_path} FLASH_IN_PROJECT DEPENDS CopyFSAndWebUI)
else()
    message(STATUS "Creating SPIFFS file system image from ${_full_fs_dest_image_path}")
    spiffs_create_partition_image(fs ${_full_fs_dest_image_path} FLASH_IN_PROJECT DEPENDS CopyFSAndWebUI)
endif()