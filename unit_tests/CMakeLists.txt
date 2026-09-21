# Raft Project
cmake_minimum_required(VERSION 3.16)

# Use local RaftCore (parent directory) instead of fetching from GitHub
set(FETCHCONTENT_SOURCE_DIR_RAFTCORE "${CMAKE_SOURCE_DIR}/.." CACHE PATH "" FORCE)

# Bootstrap using the RaftBootstrap script from the local RaftCore (parent directory)
# These tests are for the local RaftCore so both stages of the bootstrap come from it - unlike an app which
# downloads RaftBootstrap.cmake (stage 1) from the RaftCore release matching RaftCore@<tag> in features.cmake
include("${CMAKE_SOURCE_DIR}/../scripts/RaftBootstrap.cmake")

# Add parent directory to find components
list(APPEND EXTRA_COMPONENT_DIRS "..")

# Define the project dependencies
project(${_systype_name} DEPENDS ${ADDED_PROJECT_DEPENDENCIES})
