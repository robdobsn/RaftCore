# Set the target Espressif chip
set(IDF_TARGET "esp32s3")

# Raft components
set(RAFT_COMPONENTS
    RaftCore@main
)

# File system
set(FS_TYPE "littlefs")
set(FS_IMAGE_PATH "../unittest/FSImage")
