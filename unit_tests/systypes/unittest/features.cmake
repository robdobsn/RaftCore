# Set the target Espressif chip
set(IDF_TARGET "esp32")

# System version
add_compile_definitions(SYSTEM_VERSION="1.0.0")

# Raft components
set(RAFT_COMPONENTS
)

# File system
set(FS_TYPE "littlefs")
set(FS_IMAGE_PATH "../unittest/FSImage")
