# This is the project CMakeLists.txt file for the test subproject
cmake_minimum_required(VERSION 3.16)

# Include the library root folder which contains the CMakelists.txt file
list(APPEND EXTRA_COMPONENT_DIRS ".." )

# Enable network functionality for the tests
add_compile_definitions(FEATURE_NETWORK_FUNCTIONALITY)
add_compile_definitions(JSMN_PARENT_LINKS)
add_compile_definitions(JSMN_STRICT)

# Standard project configuration stuff
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_raft_core)

