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
    else()
        set(_raft_component_tag "")
    endif()

    # Check if the component name is a full URL or just a repo name
    if(_raft_component_name MATCHES "^https?://")
        # Full URL provided (e.g., https://github.com/robdobsn/RaftCore.git)
        set(_raft_repo_url ${_raft_component_name})
        
        # Extract the repository name from the URL for the source directory
        # Remove .git suffix if present
        string(REGEX REPLACE "\\.git$" "" _raft_repo_url_no_git ${_raft_component_name})
        # Extract last component of path (repo name)
        string(REGEX REPLACE "^.*/([^/]+)$" "\\1" _raft_repo_name ${_raft_repo_url_no_git})
        
        set(_raft_component_name ${_raft_repo_name})
    else()
        # Just a repo name provided (e.g., RaftCore)
        # Construct the full GitHub URL
        set(_raft_repo_url "https://github.com/robdobsn/${_raft_component_name}.git")
    endif()
    
    # Create lowercase version for FetchContent variable names
    string(TOLOWER ${_raft_component_name} _raft_component_lower)

    # Check if a local copy of this library exists in ./raftdevlibs/<ComponentName>
    set(_local_lib_path "${CMAKE_SOURCE_DIR}/raftdevlibs/${_raft_component_name}")
    if(EXISTS "${_local_lib_path}" AND IS_DIRECTORY "${_local_lib_path}")

        # Use the local library instead of fetching from remote
        message(STATUS "")
        message(STATUS "============================================================")
        message(STATUS "  LOCAL (DEBUG) LIBRARY: ${_raft_component_name}")
        message(STATUS "  Using: ${_local_lib_path}")
        message(STATUS "  (Skipping fetch from ${_raft_repo_url})")
        message(STATUS "============================================================")
        message(STATUS "")

        # Set the _SOURCE_DIR variable that FetchContent_Populate would normally set
        # (downstream scripts like RaftBootstrapPhase2.cmake reference ${raftcore_SOURCE_DIR} etc.)
        set(${_raft_component_lower}_SOURCE_DIR "${_local_lib_path}" CACHE PATH "" FORCE)

        # Add the local library dir to extra component dirs
        set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} ${_local_lib_path})

        # Add the component to the list of dependencies
        set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} ${_raft_component_lower})

    else()

        # Fetch the Raft library from remote
        FetchContent_Declare(
            ${_raft_component_lower}
            SOURCE_DIR     ${RAFT_BUILD_ARTIFACTS_FOLDER}/${_raft_component_name}
            GIT_REPOSITORY ${_raft_repo_url}
            GIT_TAG        ${_raft_component_tag}
        )
        FetchContent_Populate(${_raft_component_lower})

        # Add the component dir to the list of extra component dirs
        set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} ${${_raft_component_lower}_SOURCE_DIR})

        # Add the component to the list of dependencies
        set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} ${_raft_component_lower})

    endif()

endforeach()

################################################
# Continue with the RaftCore script
################################################

if(RAFT_BUILD_FOR_LINUX)
    # For Linux builds, use a simplified build process
    message(STATUS "Using Linux build process")
    # Check if there's a local RaftBootstrapLinux.cmake (for development/testing)
    if(EXISTS "${CMAKE_SOURCE_DIR}/RaftBootstrapLinux.cmake")
        include("${CMAKE_SOURCE_DIR}/RaftBootstrapLinux.cmake")
    else()
        include("${raftcore_SOURCE_DIR}/scripts/RaftBootstrapLinux.cmake")
    endif()
else()
    # For ESP32 builds, use the standard Phase2 script
    include("${raftcore_SOURCE_DIR}/scripts/RaftBootstrapPhase2.cmake")
endif()
