# Generate file system images
# Rob Dobson 2024

message (STATUS "------------------ Generating file system image for ${FS_TYPE} ------------------")

if (FS_TYPE STREQUAL "littlefs")

  # Fetch the littlefs image build tool
  FetchContent_Declare(
    mklittlefs
    GIT_REPOSITORY https://github.com/earlephilhower/mklittlefs.git
    GIT_TAG        3.2.0
  )

  # Make the library and the image build tool available to the project
  FetchContent_MakeAvailable(mklittlefs)

  # Create a LittleFS image from the contents of the _full_fs_dest_image_path directory
  # that fits the partition named 'fs'. FLASH_IN_PROJECT indicates that
  # the generated image should be flashed when the entire project is flashed to
  # the target with 'idf.py -p PORT flash'.
  # include("${raftcore_SOURCE_DIR}/scripts/CreateLittleFSImage.cmake")
  message(STATUS "Creating LittleFS file system image from ${_full_fs_dest_image_path}")
  littlefs_create_partition_image(fs ${_full_fs_dest_image_path} FLASH_IN_PROJECT DEPENDS CopyFSAndWebUI)

else()

  # Create a SPIFFS image from the contents of the FS_IMAGE_PATH directory
  # that fits the partition named 'fs'. FLASH_IN_PROJECT indicates that
  # the generated image should be flashed when the entire project is flashed to
  # the target with 'idf.py -p PORT flash'.
  message(STATUS "Creating SPIFFS file system image from ${_full_fs_dest_image_path}")
  spiffs_create_partition_image(fs ${_full_fs_dest_image_path} FLASH_IN_PROJECT DEPENDS CopyFSAndWebUI)

endif()
