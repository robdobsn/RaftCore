# Common features (target chip, ESP-IDF version and Raft components)
include("${BUILD_CONFIG_DIR}/../Common/features.cmake")

# The unittest SysType with PSRAM (octal, e.g. ESP32-S3 N8R8 / N16R8) - tests that depend on PSRAM take their PSRAM branch

# File system
set(FS_TYPE "littlefs")
set(FS_IMAGE_PATH "../unittest/FSImage")

# Uncomment to build this SysType with a different ESP-IDF version from the one in Common/features.cmake
# set(ESP_IDF_VERSION "6.1")
