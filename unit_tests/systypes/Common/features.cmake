# Set the target Espressif chip
set(IDF_TARGET "esp32s3")

# ESP-IDF version used to build (for both local and Docker builds using "raft build")
# This is the default for all SysTypes - it can be overridden in systypes/<SysType>/features.cmake
# It must be a literal version on one line as it is also read by the raft command line tool
set(ESP_IDF_VERSION "6.1")

# Raft components
set(RAFT_COMPONENTS
    RaftCore@main
)
