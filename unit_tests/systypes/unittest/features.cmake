# Set the target Espressif chip
set(IDF_TARGET "esp32s3")

# ESP-IDF version used to build this SysType (for both local and Docker builds using "raft build")
# It must be a literal version on one line as it is also read by the raft command line tool
set(ESP_IDF_VERSION "6.0.1")

# Raft components
set(RAFT_COMPONENTS
    RaftCore@main
)

# File system
set(FS_TYPE "littlefs")
set(FS_IMAGE_PATH "../unittest/FSImage")
