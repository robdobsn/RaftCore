# Raft Project
cmake_minimum_required(VERSION 3.16)

# Use local RaftCore (parent directory) instead of fetching from GitHub
set(FETCHCONTENT_SOURCE_DIR_RAFTCORE "${CMAKE_SOURCE_DIR}/.." CACHE PATH "" FORCE)

# Bootstrap using RaftBootstrap script
set(BOOTSTRAP_URL "https://github.com/robdobsn/RaftCore/releases/download/v1.37.1/RaftBootstrap.cmake")
file(DOWNLOAD ${BOOTSTRAP_URL} "${CMAKE_BINARY_DIR}/RaftBootstrap.cmake")
include("${CMAKE_BINARY_DIR}/RaftBootstrap.cmake")

# Add parent directory to find components
list(APPEND EXTRA_COMPONENT_DIRS "..")

# Define the project dependencies
project(${_systype_name} DEPENDS ${ADDED_PROJECT_DEPENDENCIES})
