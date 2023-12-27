# This is the project CMakeLists.txt file for the test subproject
cmake_minimum_required(VERSION 3.16)

# Include the components directory of the main application:
list(APPEND EXTRA_COMPONENT_DIRS "tests" ".." )

# Enable network functionality for the tests
add_compile_definitions(FEATURE_NETWORK_FUNCTIONALITY)

# Standard project configuration stuff
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(unittests)

# Set compile flags to enable parents in jsmn
idf_build_set_property(COMPILE_OPTIONS "-DJSMN_PARENT_LINKS -DJSMN_STRICT" APPEND)
