# Common features (target chip, ESP-IDF version and Raft components)
include("${BUILD_CONFIG_DIR}/../Common/features.cmake")

# File system
set(FS_TYPE "littlefs")
set(FS_IMAGE_PATH "../unittest/FSImage")

# Uncomment to build this SysType with a different ESP-IDF version from the one in Common/features.cmake
# set(ESP_IDF_VERSION "6.1")
