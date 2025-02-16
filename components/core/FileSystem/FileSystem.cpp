/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FileSystem
// Handles SPIFFS/LittleFS and SD card file access
//
// Rob Dobson 2018-2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <sys/stat.h>
#include <sys/unistd.h>
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "esp_err.h"
#include "dirent.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "driver/sdspi_host.h"
#include "esp_idf_version.h"
#include "FileSystem.h"
#include "RaftUtils.h"
#include "ConfigPinMap.h"
#include "Logger.h"
#include "SpiramAwareAllocator.h"
#ifdef FILE_SYSTEM_SUPPORTS_LITTLEFS
#include "esp_littlefs.h"
#endif

FileSystem fileSystem;

// Warn
#define WARN_ON_FILE_SYSTEM_BUSY
#define WARN_ON_FILE_SYSTEM_ERRORS
#define WARN_ON_INVALID_FILE_SYSTEM
#define WARN_ON_FILE_TOO_BIG
#define WARN_ON_GET_CONTENTS_IS_FOLDER

// Debug
// #define DEBUG_FILE_NOT_FOUND
// #define DEBUG_FILE_UPLOAD
// #define DEBUG_GET_FILE_CONTENTS
// #define DEBUG_FILE_EXISTS_PERFORMANCE
// #define DEBUG_FILE_SYSTEM_MOUNT
// #define DEBUG_CACHE_FS_INFO
// #define DEBUG_FILE_SYSTEM_WRITE_PERFORMANCE
// #define DEBUG_FILE_INFO_NOT_FOUND
#define DEBUG_TIME_GET_FILE_INFO

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
FileSystem::FileSystem()
{
    _fileSysMutex = xSemaphoreCreateMutex();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
FileSystem::~FileSystem()
{
    if (_fileSysMutex)
        vSemaphoreDelete(_fileSysMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup the file system, including local and SD card file systems.
/// @param localFsDefaultType Default type of the local file system (SPIFFS, LittleFS, or Disabled).
/// @param localFsFormatIfCorrupt Format the local file system if it is found to be corrupt.
/// @param enableSD Enable the SD card file system.
/// @param sdMOSIPin MOSI pin for SD card.
/// @param sdMISOPin MISO pin for SD card.
/// @param sdCLKPin Clock pin for SD card.
/// @param sdCSPin Chip select pin for SD card.
/// @param defaultToSDIfAvailable Use the SD card as the default file system if it is available.
/// @param cacheFileSystemInfo Enable caching of file system info.
void FileSystem::setup(LocalFileSystemType localFsDefaultType, bool localFsFormatIfCorrupt, bool enableSD,
                       int sdMOSIPin, int sdMISOPin, int sdCLKPin, int sdCSPin, bool defaultToSDIfAvailable,
                       bool cacheFileSystemInfo)
{
    // Initialize configuration variables
    _localFsType = localFsDefaultType;                // Set the local file system type
    _cacheFileSystemInfo = cacheFileSystemInfo;       // Enable or disable file system info caching
    _defaultToSDIfAvailable = defaultToSDIfAvailable; // Use SD as default if available

    // Initialize the local file system (e.g., SPIFFS or LittleFS)
    localFileSystemSetup(localFsFormatIfCorrupt);

    // Initialize the SD card file system if enabled
    sdFileSystemSetup(enableSD, sdMOSIPin, sdMISOPin, sdCLKPin, sdCSPin);

    // Perform initial servicing of the file system to prime caches
    for (uint32_t i = 0; i < SERVICE_COUNT_FOR_CACHE_PRIMING; i++)
    {
        loop();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Periodic service function to maintain file system caches
void FileSystem::loop()
{
    // If caching is not enabled, return early
    if (!_cacheFileSystemInfo)
    {
        return;
    }

    // Service the local file system cache
    cacheService(_localFsInfo, _localFsCache);

    // Service the SD card file system cache
    cacheService(_sdFsInfo, _sdFsCache);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Reformat the file system.
/// @param fileSystemStr File system type as a string (e.g., "local" or "sd").
/// @param respStr Response string (formatted in JSON).
/// @param force Force reformatting even if checks fail.
/// @return true if the reformat operation was successful, false otherwise.
bool FileSystem::reformat(const String &fileSystemStr, String &respStr, bool force)
{
    // Check if the local file system is disabled
    if (_localFsType == LOCAL_FS_DISABLE)
    {
        LOG_W(MODULE_PREFIX, "reformat local file system disabled");
        return false;
    }

    // Validate the file system name
    String nameOfFS;
    if (!force && !checkFileSystem(fileSystemStr, nameOfFS))
    {
        LOG_W(MODULE_PREFIX, "reformat invalid file system %s, resolved to %s, default %s",
              fileSystemStr.c_str(), nameOfFS.c_str(), getDefaultFSRoot().c_str());
        Raft::setJsonErrorResult("reformat", respStr, "invalidfs");
        return false;
    }

    // Ensure only the local file system can be formatted
    if (!force && !nameOfFS.equalsIgnoreCase(LOCAL_FILE_SYSTEM_NAME))
    {
        LOG_W(MODULE_PREFIX, "reformat only supports the local file system");
        return false;
    }

    // Reset cache validity flags
    _localFsCache.isSizeInfoValid = false;
    _localFsCache.isFileInfoValid = false;
    _localFsCache.isFileInfoSetup = false;

    // Attempt to reformat based on the local file system type
    esp_err_t ret = ESP_FAIL;
#ifdef FILE_SYSTEM_SUPPORTS_LITTLEFS
    if (_localFsType == LOCAL_FS_LITTLEFS)
    {
        ret = esp_littlefs_format(_fsPartitionName.c_str());
    }
    else
#endif
    {
        ret = esp_spiffs_format(NULL);
    }

    // Set the response and log the result
    Raft::setJsonBoolResult("reformat", respStr, ret == ESP_OK);
    LOG_W(MODULE_PREFIX, "Reformat result: %s", (ret == ESP_OK ? "OK" : "FAIL"));

    return ret == ESP_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the root path of the default file system.
/// @return The name of the root file system ("local" or "sd").
String FileSystem::getDefaultFSRoot() const
{
    // Check if SD file system is used
    if (_sdFsInfo.isUsed)
    {
        // If defaulting to SD is enabled, return SD as the root
        if (_defaultToSDIfAvailable)
        {
            return SD_FILE_SYSTEM_NAME;
        }
        // If no local file system name is set, default to SD
        if (_localFsInfo.fsName.length() == 0)
        {
            return SD_FILE_SYSTEM_NAME;
        }
    }

    // If no local file system name is set, default to local file system
    if (_localFsInfo.fsName.length() == 0)
    {
        return LOCAL_FILE_SYSTEM_NAME;
    }

    // Otherwise, return the configured local file system name
    return _localFsInfo.fsName.c_str();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get information about a specific file.
/// @param fileSystemStr The file system type as a string (e.g., "local" or "sd").
/// @param filename The name of the file to query.
/// @param fileLength Output parameter to store the file's size.
/// @return true if the file exists and its information is retrieved; false otherwise.
bool FileSystem::getFileInfo(const String &fileSystemStr, const String &filename, uint32_t &fileLength)
{
    // Validate the file system type and resolve its name
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
        LOG_W(MODULE_PREFIX, "getFileInfo %s invalid file system %s", filename.c_str(), fileSystemStr.c_str());
        return false;
    }

    // Acquire the mutex to ensure thread-safe access
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
    {
        return false;
    }

    // Build the full path for the file
    struct stat st;
    String rootFilename = getFilePath(nameOfFS, filename);

    // Check if the file exists and retrieve its information
    if (stat(rootFilename.c_str(), &st) != 0)
    {
        xSemaphoreGive(_fileSysMutex);
#ifdef DEBUG_FILE_NOT_FOUND
        LOG_I(MODULE_PREFIX, "getFileInfo %s cannot stat", rootFilename.c_str());
#endif
        return false;
    }

    // Ensure it is a regular file (not a folder or special file)
    if (!S_ISREG(st.st_mode))
    {
        xSemaphoreGive(_fileSysMutex);
#ifdef WARN_ON_FILE_SYSTEM_ERRORS
        LOG_W(MODULE_PREFIX, "getFileInfo %s is a folder", rootFilename.c_str());
#endif
        return false;
    }

    // Retrieve and store the file size
    fileLength = st.st_size;

    // Release the mutex and return success
    xSemaphoreGive(_fileSysMutex);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get a list of files on the file system as a JSON format string
/// @param req Request string
/// @param fileSystemStr File system string
/// @param folderStr Folder string
/// @param respStr String to receive response
/// @return Return code
/// @note Format of JSON is {"req":"filelist","rslt":"ok","fsName":"local","fsBase":"/local","diskSize":524288,"diskUsed":73728,"folder":"/local/","files":[{"name":".built","size":0},{"name":"index.774a82d4.js.gz","size":54676},{"name":"index.a0238169.css.gz","size":1566},{"name":"index.html.gz","size":3605}]}
RaftRetCode FileSystem::getFilesJSON(const char* req, const String& fileSystemStr, const String& folderStr, SpiramAwareString& respStr)
{
    // Validate the file system type and resolve its name
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
#ifdef WARN_ON_INVALID_FILE_SYSTEM
        LOG_W(MODULE_PREFIX, "getFilesJSON unknownFS %s", fileSystemStr.c_str());
#endif
        // Generate an error result in JSON format
        String errMsg = "unknownfs " + fileSystemStr;
        String respStrTmp;
        Raft::setJsonErrorResult(req, respStrTmp, errMsg.c_str());
        respStr = respStrTmp;
        return RAFT_FS_NOT_SETUP;
    }

    // Check if cached information can be used
    CachedFileSystem &cachedFs = nameOfFS.equalsIgnoreCase(LOCAL_FILE_SYSTEM_NAME) ? _localFsCache : _sdFsCache;
    if (cachedFs.isUsed && _cacheFileSystemInfo &&
        ((folderStr.length() == 0) || (folderStr.equalsIgnoreCase("/"))))
    {
#ifdef DEBUG_CACHE_FS_INFO
        LOG_I(MODULE_PREFIX, "getFilesJSON using cached info");
#endif
        return getFileListJsonFromCache(req, cachedFs, "/", respStr);
    }

    // Generate file info immediately if cache is not used
    return getFileListJson(req, nameOfFS, folderStr, respStr);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get file contents
// If non-null pointer returned then it must be freed by caller
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint8_t* FileSystem::getFileContents(const String &fileSystemStr, const String &filename,
                                     int maxLen)
{
    // Check file system supported
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
#ifdef WARN_ON_INVALID_FILE_SYSTEM
        LOG_W(MODULE_PREFIX, "getContents %s invalid file system %s", filename.c_str(), fileSystemStr.c_str());
#endif
        return nullptr;
    }

    // Filename
    String rootFilename = getFilePath(nameOfFS, filename);

    // Take mutex
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
        return nullptr;

    // Get file info - to check length
    struct stat st;
    if (stat(rootFilename.c_str(), &st) != 0)
    {
        xSemaphoreGive(_fileSysMutex);
#ifdef WARN_ON_FILE_NOT_FOUND
        LOG_W(MODULE_PREFIX, "getContents %s cannot stat", rootFilename.c_str());
#endif
        return nullptr;
    }
    if (!S_ISREG(st.st_mode))
    {
        xSemaphoreGive(_fileSysMutex);
#ifdef WARN_ON_GET_CONTENTS_IS_FOLDER
        LOG_I(MODULE_PREFIX, "getContents %s is a folder", rootFilename.c_str());
#endif
        return nullptr;
    }

    // Check valid
    if (maxLen <= 0)
    {
        maxLen = SpiramAwareAllocator<char>::max_allocatable() / 3;
    }
    if (st.st_size >= maxLen - 1)
    {
        xSemaphoreGive(_fileSysMutex);
#ifdef WARN_ON_FILE_TOO_BIG
        LOG_W(MODULE_PREFIX, "getContents %s free heap %d size %d too big to read", rootFilename.c_str(), maxLen, (int)st.st_size);
#endif
        return nullptr;
    }
    int fileSize = st.st_size;

    // Open file
    FILE *pFile = fopen(rootFilename.c_str(), "rb");
    if (!pFile)
    {
        xSemaphoreGive(_fileSysMutex);
#ifdef WARN_ON_FILE_NOT_FOUND
        LOG_W(MODULE_PREFIX, "getContents failed to open file to read %s", rootFilename.c_str());
#endif
        return nullptr;
    }

#ifdef DEBUG_GET_FILE_CONTENTS
    uint32_t intMemPreAlloc = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif

    // Buffer
    SpiramAwareAllocator<uint8_t> allocator;
    uint8_t *pBuf = allocator.allocate(fileSize + 1);
    if (!pBuf)
    {
        fclose(pFile);
        xSemaphoreGive(_fileSysMutex);
#ifdef WARN_ON_FILE_TOO_BIG
        LOG_W(MODULE_PREFIX, "getContents failed to allocate %d", fileSize);
#endif
        return nullptr;
    }

    // Read
    size_t bytesRead = fread(pBuf, 1, fileSize, pFile);
    fclose(pFile);
    xSemaphoreGive(_fileSysMutex);
    pBuf[bytesRead] = 0;

#ifdef DEBUG_GET_FILE_CONTENTS
    LOG_I(MODULE_PREFIX, "getContents preReadIntHeap %d postReadIntHeap %d fileSize %d filename %s", intMemPreAlloc,
          heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
          fileSize, rootFilename.c_str());
#endif

    // Return buffer - must be freed by caller
    return pBuf;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set file contents
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::setFileContents(const String &fileSystemStr, const String &filename, String &fileContents)
{
    // Check file system supported
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
        return false;
    }

    // Take mutex
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
        return false;

    // Open file for writing
    String rootFilename = getFilePath(nameOfFS, filename);
    FILE *pFile = fopen(rootFilename.c_str(), "wb");
    if (!pFile)
    {
        xSemaphoreGive(_fileSysMutex);
#ifdef WARN_ON_FILE_SYSTEM_ERRORS
        LOG_W(MODULE_PREFIX, "setContents failed to open file to write %s", rootFilename.c_str());
#endif
        return "";
    }

    // Write
    size_t bytesWritten = fwrite((uint8_t *)(fileContents.c_str()), 1, fileContents.length(), pFile);
    fclose(pFile);

    // Clean up
#ifdef DEBUG_CACHE_FS_INFO
    LOG_I(MODULE_PREFIX, "setFileContents cache invalid");
#endif
    cacheMarkDirty(nameOfFS, filename);
    xSemaphoreGive(_fileSysMutex);
    return bytesWritten == fileContents.length();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Delete
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::deleteFile(const String &fileSystemStr, const String &filename)
{
    // Check file system supported
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
        return false;
    }

    // Take mutex
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
        return false;

    // Remove file
    struct stat st;
    String rootFilename = getFilePath(nameOfFS, filename);
    if (stat(rootFilename.c_str(), &st) == 0)
    {
        unlink(rootFilename.c_str());
    }

#ifdef DEBUG_CACHE_FS_INFO
    LOG_I(MODULE_PREFIX, "deleteFile cache invalid");
#endif
    cacheMarkDirty(nameOfFS, filename);
    xSemaphoreGive(_fileSysMutex);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Read line
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

char *FileSystem::readLineFromFile(char *pBuf, int maxLen, FILE *pFile)
{
    // Iterate over chars
    pBuf[0] = 0;
    char *pCurPtr = pBuf;
    int curLen = 0;
    while (true)
    {
        if (curLen >= maxLen - 1)
            break;
        int ch = fgetc(pFile);
        if (ch == EOF)
        {
            if (curLen != 0)
                break;
            return NULL;
        }
        if (ch == '\n')
            break;
        if (ch == '\r')
            continue;
        *pCurPtr++ = ch;
        *pCurPtr = 0;
        curLen++;
    }
    return pBuf;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Read line from file
/// @param pFile File pointer
/// @param maxLen Maximum length of line
/// @return String containing line
String FileSystem::readLineFromFile(FILE *pFile, int maxLen)
{
    // Build the string
    String lineStr;
    int curLen = 0;
    while (true)
    {
        if (curLen >= maxLen - 1)
            return lineStr;
        int ch = fgetc(pFile);
        if (ch == EOF)
            return lineStr;
        if (ch == '\n')
            return lineStr;
        if (ch == '\r')
            continue;
        lineStr += (char)ch;
        curLen++;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get file name extension
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String FileSystem::getFileExtension(const String &fileName)
{
    String extn;
    // Find last .
    int dotPos = fileName.lastIndexOf('.');
    if (dotPos < 0)
        return extn;
    // Return substring
    return fileName.substring(dotPos + 1);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get file system and check ok
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::checkFileSystem(const String &fileSystemStr, String &fsName) const
{
    // Check file system
    fsName = fileSystemStr;
    fsName.trim();
    fsName.toLowerCase();

    // Check alternate name
    if (fsName.equalsIgnoreCase(LOCAL_FILE_SYSTEM_ALT_NAME))
        fsName = LOCAL_FILE_SYSTEM_NAME;

    // Use default file system if not specified
    if (fsName.length() == 0)
        fsName = getDefaultFSRoot();

    // Handle local file system
    if (fsName == LOCAL_FILE_SYSTEM_NAME)
        return _localFsCache.fsName.length() > 0;

    // SD file system
    if (fsName == SD_FILE_SYSTEM_NAME)
    {
        if (!_sdFsCache.isUsed)
            return false;
        return true;
    }

    // Unknown FS
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get file path
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String FileSystem::getFilePath(const String &nameOfFS, const String &filename) const
{
    // Check if filename already contains file system
    if ((filename.indexOf(LOCAL_FILE_SYSTEM_PATH_ELEMENT) >= 0) || (filename.indexOf(SD_FILE_SYSTEM_PATH_ELEMENT) >= 0))
        return (filename.startsWith("/") ? filename : ("/" + filename));
    return (filename.startsWith("/") ? "/" + nameOfFS + filename : ("/" + nameOfFS + "/" + filename));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get file path with fs check
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::getFileFullPath(const String &filename, String &fileFullPath) const
{
    // Extract filename elements
    String modFileName = filename;
    String fsName;
    modFileName.trim();
    int firstSlashPos = modFileName.indexOf('/');
    if (firstSlashPos > 0)
    {
        fsName = modFileName.substring(0, firstSlashPos);
        modFileName = modFileName.substring(firstSlashPos + 1);
    }

    // Check the file system is valid
    String nameOfFS;
    if (!checkFileSystem(fsName, nameOfFS))
    {
        LOG_W(MODULE_PREFIX, "getFileFullPath %s invalid file system %s",
              filename.c_str(), fsName.c_str());
        fileFullPath = filename;
        return false;
    }

    // Form the full path
    fileFullPath = getFilePath(nameOfFS, modFileName);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Exists - check if file exists
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::exists(const char *path) const
{
    // Take mutex
#ifdef DEBUG_FILE_EXISTS_PERFORMANCE
    uint64_t st1 = micros();
#endif
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
        return false;
#ifdef DEBUG_FILE_EXISTS_PERFORMANCE
    uint64_t st2 = micros();
#endif
    struct stat buffer;
    bool rslt = (stat(path, &buffer) == 0);
#ifdef DEBUG_FILE_EXISTS_PERFORMANCE
    uint64_t st3 = micros();
#endif
    xSemaphoreGive(_fileSysMutex);
#ifdef DEBUG_FILE_EXISTS_PERFORMANCE
    uint64_t st4 = micros();
    LOG_I(MODULE_PREFIX, "exists 1:%lld 2:%lld 3:%lld", st2 - st1, st3 - st2, st4 - st3);
#endif
    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Path type - folder/file/non-existent
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileSystem::FileSystemStatType FileSystem::pathType(const char *filename)
{
    // Take mutex
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
        return FILE_SYSTEM_STAT_NO_EXIST;
    struct stat buffer;
    bool rslt = stat(filename, &buffer);
    xSemaphoreGive(_fileSysMutex);
    if (rslt != 0)
        return FILE_SYSTEM_STAT_NO_EXIST;
    if (S_ISREG(buffer.st_mode))
        return FILE_SYSTEM_STAT_FILE;
    return FILE_SYSTEM_STAT_DIR;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get a section of a file
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::getFileSection(const String &fileSystemStr, const String &filename, uint32_t sectionStart, uint8_t *pBuf,
                                uint32_t sectionLen, uint32_t &readLen)
{
    // Check file system supported
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
        LOG_W(MODULE_PREFIX, "getFileSection %s invalid file system %s", filename.c_str(), fileSystemStr.c_str());
        return false;
    }

    // Take mutex
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
        return false;

    // Open file
    String rootFilename = getFilePath(nameOfFS, filename);
    FILE *pFile = fopen(rootFilename.c_str(), "rb");
    if (!pFile)
    {
        xSemaphoreGive(_fileSysMutex);
        LOG_W(MODULE_PREFIX, "getFileSection failed to open file to read %s", rootFilename.c_str());
        return false;
    }

    // Move to appropriate place in file
    fseek(pFile, sectionStart, SEEK_SET);

    // Read
    readLen = fread((char *)pBuf, 1, sectionLen, pFile);
    fclose(pFile);
    xSemaphoreGive(_fileSysMutex);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get a section of a file
/// @param fileSystemStr File system string
/// @param filename Filename
/// @param sectionStart Start position in file
/// @param sectionLen Length of section to read
/// @param readLen Length actually read
/// @return true if successful
SpiramAwareUint8Vector FileSystem::getFileSection(const String &fileSystemStr, const String &filename, uint32_t sectionStart, uint32_t sectionLen)
{
    // Check file system supported
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
        LOG_W(MODULE_PREFIX, "getFileSection %s invalid file system %s", filename.c_str(), fileSystemStr.c_str());
        return SpiramAwareUint8Vector();
    }

    // Take mutex
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
        return SpiramAwareUint8Vector();

    // Open file
    String rootFilename = getFilePath(nameOfFS, filename);
    FILE *pFile = fopen(rootFilename.c_str(), "rb");
    if (!pFile)
    {
        xSemaphoreGive(_fileSysMutex);
        LOG_W(MODULE_PREFIX, "getFileSection failed to open file to read %s", rootFilename.c_str());
        return SpiramAwareUint8Vector();
    }

    // Move to appropriate place in file
    fseek(pFile, sectionStart, SEEK_SET);

    // Read
    SpiramAwareUint8Vector fileData;
    fileData.resize(sectionLen);
    int readLen = fread((char *)fileData.data(), 1, fileData.size(), pFile);
    fclose(pFile);
    xSemaphoreGive(_fileSysMutex);

    // Return data
    if (readLen <= 0)
        return SpiramAwareUint8Vector();
    else if (readLen < fileData.size())
        fileData.resize(readLen);
    return fileData;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get a line from a text file
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::getFileLine(const String &fileSystemStr, const String &filename, uint32_t startFilePos, uint8_t *pBuf,
                             uint32_t lineMaxLen, uint32_t &fileCurPos)
{
    // Check file system supported
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
        LOG_W(MODULE_PREFIX, "getFileLine %s invalid file system %s", filename.c_str(), fileSystemStr.c_str());
        return false;
    }

    // Take mutex
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
        return false;

    // Open file for text reading
    String rootFilename = getFilePath(nameOfFS, filename);
    FILE *pFile = fopen(rootFilename.c_str(), "r");
    if (!pFile)
    {
        xSemaphoreGive(_fileSysMutex);
        LOG_W(MODULE_PREFIX, "getFileLine failed to open file to read %s", rootFilename.c_str());
        return false;
    }

    // Move to appropriate place in file
    fseek(pFile, startFilePos, SEEK_SET);

    // Read line
    char *pReadLine = readLineFromFile((char *)pBuf, lineMaxLen - 1, pFile);

    // Record final
    fileCurPos = ftell(pFile);

    // Close
    fclose(pFile);
    xSemaphoreGive(_fileSysMutex);

    // Ok if we got something
    return pReadLine != NULL;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get a line from a text file
/// @param fileSystemStr File system string
/// @param filename Filename
/// @param startFilePos Start position in file
/// @param pBuf Buffer to read into
/// @param lineMaxLen Maximum length of line
/// @param fileCurPos Current position in file
/// @return true if successful
String FileSystem::getFileLine(const String &fileSystemStr, const String &filename, uint32_t startFilePos, uint32_t lineMaxLen, uint32_t &fileCurPos)
{
    // Check file system supported
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
        LOG_W(MODULE_PREFIX, "getFileLine %s invalid file system %s", filename.c_str(), fileSystemStr.c_str());
        return "";
    }

    // Take mutex
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
        return "";

    // Open file for text reading
    String rootFilename = getFilePath(nameOfFS, filename);
    FILE *pFile = fopen(rootFilename.c_str(), "r");
    if (!pFile)
    {
        xSemaphoreGive(_fileSysMutex);
        LOG_W(MODULE_PREFIX, "getFileLine failed to open file to read %s", rootFilename.c_str());
        return "";
    }

    // Move to appropriate place in file
    fseek(pFile, startFilePos, SEEK_SET);

    // Read line
    String line = readLineFromFile(pFile, lineMaxLen);

    // Record final
    fileCurPos = ftell(pFile);

    // Close
    fclose(pFile);
    xSemaphoreGive(_fileSysMutex);

    // Return line
    return line;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Open file
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FILE *FileSystem::fileOpen(const String &fileSystemStr, const String &filename,
                           bool writeMode, uint32_t seekToPos, bool seekFromEnd)
{
#ifdef DEBUG_FILE_SYSTEM_WRITE_PERFORMANCE
    uint32_t startMs = millis();
#endif

    // Check file system supported
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
        LOG_W(MODULE_PREFIX, "fileOpen %s invalid file system %s", filename.c_str(), fileSystemStr.c_str());
        return nullptr;
    }

    // Take mutex
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
        return nullptr;

#ifdef DEBUG_FILE_SYSTEM_WRITE_PERFORMANCE
    uint32_t mutexMs = millis() - startMs;
    startMs = millis();
#endif

    // Open file
    String rootFilename = getFilePath(nameOfFS, filename);

#ifdef DEBUG_FILE_SYSTEM_WRITE_PERFORMANCE
    uint32_t getFilePathMs = millis() - startMs;
    startMs = millis();
#endif

    FILE *pFile = fopen(rootFilename.c_str(), writeMode ? ((seekToPos != 0) || seekFromEnd ? "ab" : "wb") : "rb");

#ifdef DEBUG_FILE_SYSTEM_WRITE_PERFORMANCE
    uint32_t fopenMs = millis() - startMs;
    startMs = millis();
#endif

    if (pFile && ((seekToPos != 0) || seekFromEnd))
    {
        fseek(pFile, seekToPos, seekFromEnd ? SEEK_END : SEEK_SET);
    }

#ifdef DEBUG_FILE_SYSTEM_WRITE_PERFORMANCE
    uint32_t fseekMs = millis() - startMs;
    startMs = millis();
#endif

    // Release mutex
    xSemaphoreGive(_fileSysMutex);

#ifdef DEBUG_FILE_SYSTEM_WRITE_PERFORMANCE
    uint32_t releaseMutexMs = millis() - startMs;
    startMs = millis();

    LOG_I(MODULE_PREFIX, "semaphore %dms, getFilePath %dms, fopen %dms, fseek %dms, releaseMutex %dms",
          mutexMs, getFilePathMs, fopenMs, fseekMs, releaseMutexMs);
#endif

    // Check result
    if (!pFile)
    {
        LOG_W(MODULE_PREFIX, "fileOpen failed to open file to %s %s", writeMode ? "write" : "read", rootFilename.c_str());
        return nullptr;
    }

    if (writeMode)
    {
#ifdef DEBUG_CACHE_FS_INFO
        LOG_I(MODULE_PREFIX, "fileOpen cache invalid");
#endif
    }

    // Return file
    return pFile;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Close file
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::fileClose(FILE *pFile, const String &fileSystemStr, const String &filename, bool fileModified)
{
    // Check file system supported
    String nameOfFS;
    checkFileSystem(fileSystemStr, nameOfFS);

    // Take mutex
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
        return false;

    // Check if file modified
    if (fileModified)
    {
        cacheMarkDirty(nameOfFS, filename);
    }

    // Close file
    fclose(pFile);

    // Release mutex
    xSemaphoreGive(_fileSysMutex);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Read from file
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t FileSystem::fileRead(FILE *pFile, uint8_t *pBuf, uint32_t readLen)
{
    // Ensure valid
    if (!pFile)
    {
        LOG_W(MODULE_PREFIX, "fileRead filePtr null");
        return 0;
    }

    // Take mutex
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
        return 0;

    // Read
    uint32_t lenRead = fread((char *)pBuf, 1, readLen, pFile);

    // Release mutex
    xSemaphoreGive(_fileSysMutex);
    return lenRead;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Read from file
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SpiramAwareUint8Vector FileSystem::fileRead(FILE *pFile, uint32_t readLen)
{
    // Ensure valid
    if (!pFile)
    {
        LOG_W(MODULE_PREFIX, "fileRead filePtr null");
        return SpiramAwareUint8Vector();
    }

    // Take mutex
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
        return SpiramAwareUint8Vector();

    // Read
    SpiramAwareUint8Vector fileData;
    fileData.resize(readLen);
    uint32_t lenRead = fread((char *)fileData.data(), 1, fileData.size(), pFile);

    // Release mutex
    xSemaphoreGive(_fileSysMutex);

    // Check for error
    if (lenRead == 0)
        return SpiramAwareUint8Vector();
    else if (lenRead < readLen)
        fileData.resize(lenRead);
    return fileData;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Write to file
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t FileSystem::fileWrite(FILE *pFile, const uint8_t *pBuf, uint32_t writeLen)
{
    // Ensure valid
    if (!pFile)
    {
        LOG_W(MODULE_PREFIX, "fileWrite filePtr null");
        return 0;
    }

    // Take mutex
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
        return 0;

    // Read
    uint32_t lenWritten = fwrite((char *)pBuf, 1, writeLen, pFile);

    // Release mutex
    xSemaphoreGive(_fileSysMutex);
    return lenWritten;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get temporary file name
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String getTempFileName()
{
    return "__temp__";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get file position
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t FileSystem::filePos(FILE *pFile)
{
    // Ensure valid
    if (!pFile)
    {
        LOG_W(MODULE_PREFIX, "filePos filePtr null");
        return 0;
    }

    // Take mutex
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
        return 0;

    // Read
    uint32_t filePosition = ftell(pFile);

    // Release mutex
    xSemaphoreGive(_fileSysMutex);
    return filePosition;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Seek file
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::fileSeek(FILE *pFile, uint32_t seekPos)
{
    // Ensure valid
    if (!pFile)
    {
        LOG_W(MODULE_PREFIX, "fileSeek filePtr null");
        return false;
    }

    // Take mutex
    if (xSemaphoreTake(_fileSysMutex, portMAX_DELAY) != pdTRUE)
        return false;

    // Seek
    fseek(pFile, seekPos, SEEK_SET);

    // Release mutex
    xSemaphoreGive(_fileSysMutex);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup local file system
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileSystem::localFileSystemSetup(bool formatIfCorrupt)
{
    // Check enabled
    if (_localFsType == LOCAL_FS_DISABLE)
    {
        LOG_I(MODULE_PREFIX, "localFileSystemSetup local file system disabled");
        return;
    }

    // Try LittleFS first (with format if corrupt false)
#ifdef FILE_SYSTEM_SUPPORTS_LITTLEFS
    // Init LittleFS file system (format if required)
    if (localFileSystemSetupLittleFS(false))
    {
        LOG_I(MODULE_PREFIX, "localFileSystemSetup LittleFS initialised ok");
        return;
    }
#endif

    // Try SPIFFS next (with format if corrupt false)
    if (localFileSystemSetupSPIFFS(false))
    {
        LOG_I(MODULE_PREFIX, "localFileSystemSetup SPIFFS initialised ok");
        return;
    }

    // If format if corrupt is false then we are done and FS can't be mounted
    if (!formatIfCorrupt)
    {
        _localFsType = LOCAL_FS_DISABLE;
        LOG_I(MODULE_PREFIX, "localFileSystemSetup no file system found");
        return;
    }

    // Now try default FS with format if corrupt true
    if (_localFsType == LOCAL_FS_SPIFFS)
    {
        if (localFileSystemSetupSPIFFS(true))
        {
            LOG_I(MODULE_PREFIX, "localFileSystemSetup SPIFFS formmatted ok");
            return;
        }
    }
    else
    {
#ifdef FILE_SYSTEM_SUPPORTS_LITTLEFS
        if (localFileSystemSetupLittleFS(true))
        {
            LOG_I(MODULE_PREFIX, "localFileSystemSetup LittleFS formmatted ok");
            return;
        }
    }
#endif

    // Failed
    _localFsType = LOCAL_FS_DISABLE;
    LOG_W(MODULE_PREFIX, "localFileSystemSetup failed to initialise file system");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup local file system
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef FILE_SYSTEM_SUPPORTS_LITTLEFS
bool FileSystem::localFileSystemSetupLittleFS(bool formatIfCorrupt)
{
    // Set partition name
    _fsPartitionName = LOCAL_FILE_SYSTEM_PARTITION_LABEL;

    // Using ESP IDF virtual file system
    esp_vfs_littlefs_conf_t conf = {
        .base_path = LOCAL_FILE_SYSTEM_BASE_PATH,
        .partition_label = _fsPartitionName.c_str(),
        .partition = NULL,
        .format_if_mount_failed = formatIfCorrupt,
        .read_only = false,
        .dont_mount = false,
        .grow_on_mount = false,
    };
    // Use settings defined above to initialize and mount LittleFS filesystem.
    // Note: esp_vfs_littlefs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK)
    {
        // Try the alternate partition
        _fsPartitionName = LOCAL_FILE_SYSTEM_PARTITION_LABEL_ALT;
        conf.partition_label = _fsPartitionName.c_str();
        ret = esp_vfs_littlefs_register(&conf);
        if (ret != ESP_OK)
        {
#ifdef DEBUG_FILE_SYSTEM_MOUNT
            if (ret == ESP_FAIL)
            {
                LOG_I(MODULE_PREFIX, "setup failed mount/format LittleFS");
            }
            else if (ret == ESP_ERR_NOT_FOUND)
            {
                LOG_I(MODULE_PREFIX, "setup failed to find LittleFS partition");
            }
            else
            {
                LOG_I(MODULE_PREFIX, "setup failed to init LittleFS (error %s)", esp_err_to_name(ret));
            }
#endif
        }
        return false;
    }

    // Get file system info
    size_t total = 0, used = 0;
    ret = esp_littlefs_info(_fsPartitionName.c_str(), &total, &used);
    if (ret != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "setup failed to get LittleFS info (error %s)", esp_err_to_name(ret));
        return false;
    }

    // Done ok
    LOG_I(MODULE_PREFIX, "setup LittleFS partition size total %d, used %d", total, used);

    // Local file system is ok
    _localFsType = LOCAL_FS_LITTLEFS;
    _localFsCache.isFileInfoValid = false;
    _localFsCache.isSizeInfoValid = false;
    _localFsCache.isFileInfoSetup = false;
    _localFsCache.fsName = LOCAL_FILE_SYSTEM_NAME;
    _localFsCache.fsBase = LOCAL_FILE_SYSTEM_BASE_PATH;
    return true;
}
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup local file system
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::localFileSystemSetupSPIFFS(bool formatIfCorrupt)
{
    // Set partition name
    _fsPartitionName = LOCAL_FILE_SYSTEM_PARTITION_LABEL;

    // Using ESP IDF virtual file system
    esp_vfs_spiffs_conf_t conf = {
        .base_path = LOCAL_FILE_SYSTEM_BASE_PATH,
        .partition_label = _fsPartitionName.c_str(),
        .max_files = 5,
        .format_if_mount_failed = formatIfCorrupt};
    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        // Try the alternate partition
        _fsPartitionName = LOCAL_FILE_SYSTEM_PARTITION_LABEL_ALT;
        conf.partition_label = _fsPartitionName.c_str();
        ret = esp_vfs_spiffs_register(&conf);
        if (ret != ESP_OK)
        {
#ifdef DEBUG_FILE_SYSTEM_MOUNT
            if (ret == ESP_FAIL)
            {
                LOG_I(MODULE_PREFIX, "setup failed mount/format SPIFFS");
            }
            else if (ret == ESP_ERR_NOT_FOUND)
            {
                LOG_I(MODULE_PREFIX, "setup failed to find SPIFFS partition");
            }
            else
            {
                LOG_I(MODULE_PREFIX, "setup failed to init SPIFFS (error %s)", esp_err_to_name(ret));
            }
#endif
        }
        return false;
    }

    // Get file system info
    LOG_I(MODULE_PREFIX, "setup SPIFFS initialised ok");
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(_fsPartitionName.c_str(), &total, &used);
    if (ret != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "setup failed to get SPIFFS info (error %s)", esp_err_to_name(ret));
        return false;
    }

    // Done ok
    LOG_I(MODULE_PREFIX, "setup SPIFFS partition size total %d, used %d", total, used);

    // Local file system is ok
    _localFsType = LOCAL_FS_SPIFFS;
    _localFsCache.isUsed = true;
    _localFsCache.isFileInfoValid = false;
    _localFsCache.isSizeInfoValid = false;
    _localFsCache.isFileInfoSetup = false;
    _localFsCache.fsName = LOCAL_FILE_SYSTEM_NAME;
    _localFsCache.fsBase = LOCAL_FILE_SYSTEM_BASE_PATH;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup SD file system
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::sdFileSystemSetup(bool enableSD, int sdMOSIPin, int sdMISOPin, int sdCLKPin, int sdCSPin)
{
    if (!enableSD)
    {
        LOG_I(MODULE_PREFIX, "sdFileSystemSetup SD disabled");
        return false;
    }

    // Check valid
    if (sdMOSIPin == -1 || sdMISOPin == -1 || sdCLKPin == -1 || sdCSPin == -1)
    {
        LOG_W(MODULE_PREFIX, "sdFileSystemSetup SD pins invalid");
        return false;
    }

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and formatted
    // in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        .disk_status_check_enable = false,
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
        .use_one_fat = false
#endif
    };

    // Setup SD using SPI (single bit data)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
#pragma GCC diagnostic pop
    sdmmc_card_t *pCard = nullptr;

    // Handle differences in ESP IDF versions
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    // Bus config
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = (gpio_num_t)sdMOSIPin,
        .miso_io_num = (gpio_num_t)sdMISOPin,
        .sclk_io_num = (gpio_num_t)sdCLKPin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
        .data_io_default_level = 0,
#endif
        .max_transfer_sz = 4000,
        .flags = 0,
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0) && ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 2, 0)
        .isr_cpu_id = INTR_CPU_ID_AUTO,
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)

        .intr_flags = 0};
    esp_err_t ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "sdFileSystemSetup failed to init SPI");
        return false;
    }

    // Device config
    sdspi_device_config_t dev_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev_config.gpio_cs = (gpio_num_t)sdCSPin;
    dev_config.host_id = (spi_host_device_t)host.slot;

    // Mount SD card
    ret = esp_vfs_fat_sdspi_mount(SD_FILE_SYSTEM_BASE_PATH, &host, &dev_config, &mount_config, &pCard);
#else
        sdspi_slot_config_t dev_config = SDSPI_SLOT_CONFIG_DEFAULT();
        dev_config.gpio_miso = (gpio_num_t)sdMISOPin;
        dev_config.gpio_mosi = (gpio_num_t)sdMOSIPin;
        dev_config.gpio_sck = (gpio_num_t)sdCLKPin;
        dev_config.gpio_cs = (gpio_num_t)sdCSPin;

        esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_FILE_SYSTEM_BASE_PATH, &host, &dev_config, &mount_config, &pCard);
#endif

    // Check file system mounted ok
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            LOG_W(MODULE_PREFIX, "sdFileSystemSetup failed mount SD");
        }
        else
        {
            LOG_I(MODULE_PREFIX, "sdFileSystemSetup failed to init SD (error %s)", esp_err_to_name(ret));
        }
        return false;
    }

    // Save card info
    _pSDCard = pCard;
    LOG_I(MODULE_PREFIX, "sdFileSystemSetup mounted ok");

    // SD ok
    _sdFsCache.isUsed = true;
    _sdFsCache.isFileInfoValid = false;
    _sdFsCache.isSizeInfoValid = false;
    _sdFsCache.isFileInfoSetup = false;
    _sdFsCache.fsName = SD_FILE_SYSTEM_NAME;
    _sdFsCache.fsBase = SD_FILE_SYSTEM_BASE_PATH;

    // DEBUG SD print SD card info
    // sdmmc_card_print_info(stdout, pCard);

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get file system cache info in JSON
/// @param req Request string
/// @param cachedFs Cached file system
/// @param folderStr Folder string
/// @param respBuf Response buffer
/// @return Return code
RaftRetCode FileSystem::getFileListJsonFromCache(const char *req, CachedFileSystem &cachedFs, const String &folderStr, SpiramAwareUint8Vector &respBuf)
{
    // Ensure cached file system info is valid
    if (!cachedFs.isSizeInfoValid || !cachedFs.isFileInfoValid)
    {
#ifdef DEBUG_CACHE_FS_INFO
        LOG_I(MODULE_PREFIX, "getFileListJsonFromCache: Cache is invalid, need to wait.");
#endif
        return RAFT_FS_OTHER_ERROR;
    }

#ifdef DEBUG_TIMING_FS_INFO
    // Debug timing start
    uint32_t debugStartMs = millis();
#endif

    // Start JSON with file system info
    String fsInfoJson = getFsInfoJson(req, cachedFs.fsName.c_str(), "", folderStr);

    // Add to response buffer
    const char *openingBrace = "{";
    respBuf.insert(respBuf.end(), openingBrace, openingBrace + strlen(openingBrace));
    respBuf.insert(respBuf.end(), fsInfoJson.c_str(), fsInfoJson.c_str() + fsInfoJson.length());

    // Add files array start
    const char *fileListJson = R"(,"files":[)";
    respBuf.insert(respBuf.end(), fileListJson, fileListJson + strlen(fileListJson));

    // Build the cached file list JSON
    bool firstFile = true;
    for (const CachedFileInfo &cachedFileInfo : cachedFs.cachedRootFileList)
    {
        // Skip invalid cached entries
        if (!cachedFileInfo.isValid)
            continue;

        // File info as JSON string
        String fileInfoStr = (firstFile ? "" : ",") +
                             R"({"name":")" + cachedFileInfo.fileName.c_str() +
                             R"(","size":)" + String(cachedFileInfo.fileSize) + "}";

        // Append to response buffer
        respBuf.insert(respBuf.end(), fileInfoStr.c_str(), fileInfoStr.c_str() + fileInfoStr.length());
        firstFile = false;
    }

    // Close JSON
    const char *closingBraces = "]}";
    respBuf.insert(respBuf.end(), closingBraces, closingBraces + strlen(closingBraces));

#ifdef DEBUG_TIMING_FS_INFO
    // Debug timing end
    uint32_t elapsedMs = millis() - debugStartMs;
    LOG_I(MODULE_PREFIX, "getFileListJsonFromCache: JSON generation took %dms", elapsedMs);
#endif

    // Return success
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Generate file list from file system (no cache)
/// @param req Request string
/// @param fsName File system name
/// @param folderStr Folder string
/// @param respBuf Response string
/// @return Return code
RaftRetCode FileSystem::getFileListJson(const char *req, const FileSystemInfo& fsInfo, const String &folderStr, SpiramAwareUint8Vector &respBuf)
{
    // Construct the root folder path
    String fsName = fsInfo;
    String rootFolder = (fsName.startsWith("/") ? fsName : "/" + fsName) + (folderStr.startsWith("/") ? folderStr : "/" + folderStr);

    // Attempt to acquire the file system mutex for exclusive access.
    if (xSemaphoreTake(_fileSysMutex, pdMS_TO_TICKS(10)) != pdTRUE)
    {
#ifdef WARN_ON_FILE_SYSTEM_BUSY
        LOG_W(MODULE_PREFIX, "getFileListJson: File system busy");
#endif
        return RAFT_FS_BUSY;
    }

#ifdef DEBUG_TIME_GET_FILE_INFO
    // Debug timing start
    uint32_t debugStartMs = millis();
#endif

    // Verify that the file system has valid size information.
    if (cachedFs.fsSizeBytes == 0)
    {
        xSemaphoreGive(_fileSysMutex);
#ifdef WARN_ON_INVALID_FILE_SYSTEM
        LOG_W(MODULE_PREFIX, "getFileListJson: No valid file system");
#endif
        return RAFT_FS_NOT_SETUP;
    }

    // Attempt to open the directory.
    DIR *dir = opendir(rootFolder.c_str());
    if (!dir)
    {
        xSemaphoreGive(_fileSysMutex);
#ifdef DEBUG_FILE_INFO_NOT_FOUND
        LOG_I(MODULE_PREFIX, "getFileListJson: Failed to open folder %s", rootFolder.c_str());
#endif
        return RAFT_FS_FOLDER_NOT_FOUND;
    }

    // Start building the JSON response
    const char *openingBrace = "{";
    respBuf.insert(respBuf.end(), openingBrace, openingBrace + strlen(openingBrace));

    // Add file system info
    String fsInfoJson = getFsInfoJson(req, fsName, "", rootFolder);
    respBuf.insert(respBuf.end(), fsInfoJson.c_str(), fsInfoJson.c_str() + fsInfoJson.length());

    // File list JSON
    const char *fileListJson = R"(,"files":[)";
    respBuf.insert(respBuf.end(), fileListJson, fileListJson + strlen(fileListJson));

    // Build the JSON for file list
    bool firstFile = true;
    struct dirent *ent = nullptr;
    while ((ent = readdir(dir)) != nullptr)
    {
        // Filter unwanted files
        String fName = ent->d_name;
        if (fName.equalsIgnoreCase("System Volume Information") || fName.equalsIgnoreCase("thumbs.db"))
            continue;

        // Get file size using `stat`
        size_t fileSize = 0;
        struct stat st;
        String filePath = rootFolder + (rootFolder.endsWith("/") ? "" : "/") + fName;
        if (stat(filePath.c_str(), &st) == 0)
        {
            fileSize = st.st_size;
        }

        // Append file info to response buffer.
        if (!firstFile)
        {
            const char *comma = ",";
            respBuf.insert(respBuf.end(), comma, comma + 1);
        }
        firstFile = false;

        // Format file info as JSON and append.
        String fileInfoStr = R"({"name":")" + fName + R"(","size":)" + String(fileSize) + "}";
        respBuf.insert(respBuf.end(), fileInfoStr.c_str(), fileInfoStr.c_str() + fileInfoStr.length());
    }

    // Close directory and release mutex.
    closedir(dir);
    xSemaphoreGive(_fileSysMutex);

    // Add closing brace to response buffer.
    const char *closingBraces = "]}";
    respBuf.insert(respBuf.end(), closingBraces, closingBraces + strlen(closingBraces));

#ifdef DEBUG_TIME_GET_FILE_INFO
    // Debug timing end
    uint32_t debugGetFilesMs = millis() - debugStartMs;
    LOG_I(MODULE_PREFIX, "getFileListJson: JSON generation took %dms", debugGetFilesMs);
#endif

    // Return ok
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Update file system info cache
/// @param req Request string
/// @param cachedFs Cached file system
/// @return Result code
RaftRetCode FileSystem::cacheUpdate(const char *req, CachedFileSystem &cachedFs)
{
    // Attempt to take the mutex for exclusive access to the file system
    if (xSemaphoreTake(_fileSysMutex, 0) != pdTRUE)
    {
        return RAFT_FS_BUSY;
    }

    // Start measuring the time for debugging purposes
    uint32_t debugStartMs = millis();

    // Get the name of the file system
    String fsName = cachedFs.fsName.c_str();

    // Handle local file system
    if (fsName == LOCAL_FILE_SYSTEM_NAME)
    {
        size_t sizeBytes = 0, usedBytes = 0;
        esp_err_t ret = ESP_FAIL;

#ifdef FILE_SYSTEM_SUPPORTS_LITTLEFS
        // Get size info for LittleFS
        if (_localFsType == LOCAL_FS_LITTLEFS)
        {
            ret = esp_littlefs_info(_fsPartitionName.c_str(), &sizeBytes, &usedBytes);
        }
        else
#endif
        {
            // Get size info for SPIFFS
            ret = esp_spiffs_info(_fsPartitionName.c_str(), &sizeBytes, &usedBytes);
        }

        // Check if file system info retrieval was successful
        if (ret != ESP_OK)
        {
            xSemaphoreGive(_fileSysMutex);
            LOG_W(MODULE_PREFIX, "cacheUpdate failed to get file system info (error %s)", esp_err_to_name(ret));
            return RAFT_FS_OTHER_ERROR;
        }

        // Update cached file system size information
        cachedFs.fsSizeBytes = sizeBytes;
        cachedFs.fsUsedBytes = usedBytes;
        cachedFs.isSizeInfoValid = true;
    }
    // Handle SD file system
    else if (fsName == SD_FILE_SYSTEM_NAME)
    {
        sdmmc_card_t *pCard = (sdmmc_card_t *)_pSDCard;

        // Check if the SD card is available
        if (pCard)
        {
            // Calculate total size in bytes
            cachedFs.fsSizeBytes = static_cast<double>(pCard->csd.capacity) * pCard->csd.sector_size;

            // Retrieve free space using FATFS
            FATFS *fsinfo;
            DWORD fre_clust;
            if (f_getfree("0:", &fre_clust, &fsinfo) == 0)
            {
                // Calculate used space in bytes
                cachedFs.fsUsedBytes = static_cast<double>(fsinfo->csize) *
                                       (static_cast<double>(fsinfo->n_fatent - 2) - fre_clust)
#if _MAX_SS != 512
                                       * fsinfo->ssize;
#else
                                           * 512;
#endif
            }
            cachedFs.isSizeInfoValid = true;
        }
    }

    // Release the mutex
    xSemaphoreGive(_fileSysMutex);

    // Measure elapsed time for debugging
    uint32_t debugGetFsInfoMs = millis() - debugStartMs;
    LOG_I(MODULE_PREFIX, "cacheUpdate timing fsInfo %dms", debugGetFsInfoMs);

    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Mark file cache dirty
/// @param fsName File system name (local or SD)
/// @param filename Filename to mark as dirty
void FileSystem::cacheMarkDirty(const String &fsName, const String &filename)
{
    // Determine the correct cached file system
    CachedFileSystem &cachedFs = fsName.equalsIgnoreCase(LOCAL_FILE_SYSTEM_NAME) ? _localFsCache : _sdFsCache;

    // Mark file system info as invalid
    cachedFs.isFileInfoValid = false;
    cachedFs.isSizeInfoValid = false;

    // Return early if caching is disabled or not set up
    if (!_cacheFileSystemInfo || !cachedFs.isFileInfoSetup)
        return;

    // Iterate over cached file list to find the file
    auto fileIt = std::find_if(
        cachedFs.cachedRootFileList.begin(),
        cachedFs.cachedRootFileList.end(),
        [&filename](const CachedFileInfo &fileInfo)
        {
            return fileInfo.fileName == filename.c_str();
        });

    // If file is found, mark it as invalid
    if (fileIt != cachedFs.cachedRootFileList.end())
    {
        fileIt->isValid = false;
    }
    else
    {
        // If not found, add a new entry for the file and mark as invalid
        CachedFileInfo newFileInfo;
        newFileInfo.fileName = filename.c_str();
        newFileInfo.fileSize = 0;
        newFileInfo.isValid = false;
        cachedFs.cachedRootFileList.push_back(newFileInfo);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service file system cache
/// @param cachedFs Cached file system
RaftRetCode FileSystem::cacheService(FileSystemInfo& fsInfo, CachedFileSystem& cachedFs)
{
    // Check if caching is enabled
    if (!fsInfo.isUsed)
        return RAFT_OK;

    // Update cache if required
    if (!fsInfo.isSizeInfoValid)
    {
        return cacheUpdate("", fsInfo, cachedFs);
    }

    // Update file info if required
    if (!cachedFs.isFileInfoSetup)
    {
        uint32_t debugStartMs = millis();

        // Take mutex
        if (xSemaphoreTake(_fileSysMutex, 0) != pdTRUE)
            return RAFT_FS_BUSY;

        // Clear file list
        cachedFs.cachedRootFileList.clear();

        // Iterate file info
        String rootFolder = (fsInfo.fsBase + "/").c_str();
        DIR *dir = opendir(rootFolder.c_str());
        if (!dir)
        {
            xSemaphoreGive(_fileSysMutex);
            return RAFT_FS_OK;
        }

        // Read directory entries
        struct dirent *ent = NULL;
        uint32_t fileCount = 0;
        while ((ent = readdir(dir)) != NULL)
        {
            // Check for unwanted files
            String fName = ent->d_name;
            if (fName.equalsIgnoreCase("System Volume Information"))
                continue;
            if (fName.equalsIgnoreCase("thumbs.db"))
                continue;

            // Get file info including size
            size_t fileSize = 0;
            struct stat st;
            String filePath = rootFolder + fName;
            if (stat(filePath.c_str(), &st) == 0)
            {
                fileSize = st.st_size;
            }

            // Add file to list
            CachedFileInfo fileInfo;
            fileInfo.fileName = fName.c_str();
            fileInfo.fileSize = fileSize;
            fileInfo.isValid = true;
            cachedFs.cachedRootFileList.push_back(fileInfo);
            fileCount++;
        }

        // Finished with file list
        closedir(dir);
        fsInfo.isFileInfoSetup = true;
        cachedFs.isFileInfoValid = true;
        xSemaphoreGive(_fileSysMutex);
        uint32_t debugCachedFsSetupMs = millis() - debugStartMs;
        LOG_I(MODULE_PREFIX, "fsCacheService fs %s files %d took %dms",
              cachedFs.fsName.c_str(), fileCount, debugCachedFsSetupMs);
        return RAFT_OK;
    }

    // Check file info
    if (cachedFs.isFileInfoSetup && !cachedFs.isFileInfoValid)
    {
        // Update info for specific file(s)
        // Take mutex
        if (xSemaphoreTake(_fileSysMutex, 0) != pdTRUE)
            return RAFT_FS_BUSY;
        String rootFolder = (cachedFs.fsBase + "/").c_str();
        bool allValid = true;
        for (auto it = cachedFs.cachedRootFileList.begin(); it != cachedFs.cachedRootFileList.end(); ++it)
        {
            // Check name
            if (!(it->isValid))
            {
                // Get file info including size
                struct stat st;
                String filePath = rootFolder + it->fileName.c_str();
                if (stat(filePath.c_str(), &st) == 0)
                {
                    it->fileSize = st.st_size;
                    LOG_I(MODULE_PREFIX, "fsCacheService updated %s size %d",
                          it->fileName.c_str(), (int)st.st_size);
                }
                else
                {
                    // Remove from list
                    cachedFs.cachedRootFileList.erase(it);
                    LOG_I(MODULE_PREFIX, "fsCacheService deleted %s",
                          it->fileName.c_str());
                    allValid = false;
                    break;
                }
                // File info now valid
                it->isValid = true;
                break;
            }
        }
        LOG_I(MODULE_PREFIX, "fsCacheService fileInfo %s", allValid ? "valid" : "invalid");
        cachedFs.isFileInfoValid = allValid;
        xSemaphoreGive(_fileSysMutex);
    }
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Format file system info as JSON
/// @param req Request string
/// @param fsInfo file system info
/// @param rootFolder Root folder
/// @return string containing JSON
String FileSystem::getFsInfoJson(const char *req, FileSystemInfo &fsInfo, const String &rootFolder)
{
    // Build JSON string
    String jsonStr = R"("req":")" + String(req) +
                     R"(","rslt":"ok","fsName":")" + String(fsInfo.fsName.c_str()) +
                     R"(","fsBase":")" + String(fsInfo.fsBase.c_str()) +
                     R"(","diskSize":)" + String(fsInfo.fsSizeBytes) +
                     R"(,"diskUsed":)" + String(fsInfo.fsUsedBytes) +
                     R"(,"folder":")" + rootFolder;
    return jsonStr;
}
