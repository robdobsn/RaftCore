/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FileSystem 
// Handles SPIFFS/LittleFS and SD card file access
//
// Rob Dobson 2018-2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "FileSystem.h"
#include "RaftUtils.h"
#include "Logger.h"

#include <sys/stat.h>
#include <sys/unistd.h>
#include <dirent.h>

#if !defined(__linux__)
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "esp_err.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "driver/sdspi_host.h"
#include "esp_idf_version.h"
#include "ConfigPinMap.h"
#include "SpiramAwareAllocator.h"
#ifdef FILE_SYSTEM_SUPPORTS_LITTLEFS
#include "esp_littlefs.h"
#endif
#endif

// File system
FileSystem fileSystem;

// Warn
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileSystem::FileSystem()
{
    RaftMutex_init(_fileSysMutex);
}

FileSystem::~FileSystem()
{
    RaftMutex_destroy(_fileSysMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileSystem::setup(LocalFileSystemType localFsDefaultType, bool localFsFormatIfCorrupt, bool enableSD, 
        int sdMOSIPin, int sdMISOPin, int sdCLKPin, int sdCSPin, bool defaultToSDIfAvailable,
        bool cacheFileSystemInfo)
{
    // Init
    _localFsType = localFsDefaultType;
    _cacheFileSystemInfo = cacheFileSystemInfo;
    _defaultToSDIfAvailable = defaultToSDIfAvailable;

    // Setup local file system
    localFileSystemSetup(localFsFormatIfCorrupt);

    // Setup SD file system
    sdFileSystemSetup(enableSD, sdMOSIPin, sdMISOPin, sdCLKPin, sdCSPin);

    // Service a few times to setup caches
    for (uint32_t i = 0; i < SERVICE_COUNT_FOR_CACHE_PRIMING; i++)
        loop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileSystem::loop()
{
#if !defined(__linux__)
    if (!_cacheFileSystemInfo)
        return;
    fileSystemCacheService(_localFsCache);
    fileSystemCacheService(_sdFsCache);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Reformat
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::reformat(const String& fileSystemStr, String& respStr, bool force)
{
    // Check for file system disabled
    if (_localFsType == LOCAL_FS_DISABLE)
    {
        LOG_W(MODULE_PREFIX, "reformat local file system disabled");
        return false;
    }

    // Check file system supported
    String nameOfFS;
    if (!force && !checkFileSystem(fileSystemStr, nameOfFS))
    {
        LOG_W(MODULE_PREFIX, "reformat invalid file system %s returned %s default %s", 
                        fileSystemStr.c_str(), nameOfFS.c_str(), getDefaultFSRoot().c_str());
        Raft::setJsonErrorResult("reformat", respStr, "invalidfs");
        return false;
    }
    
    // Check FS name
    if (!force && !nameOfFS.equalsIgnoreCase(LOCAL_FILE_SYSTEM_NAME))
    {
        LOG_W(MODULE_PREFIX, "Can only format local file system");
        return false;
    }

#if !defined(__linux__)

    // TODO - check WDT maybe enabled
    // Reformat - need to disable Watchdog timer while formatting
    // Watchdog is not enabled on core 1 in Arduino according to this
    // https://www.bountychannel.com/issues/44690700-watchdog-with-system-reset
    // disableCore0WDT();
    _localFsCache.isSizeInfoValid = false;
    _localFsCache.isFileInfoValid = false;
    _localFsCache.isFileInfoSetup = false;
    esp_err_t ret = ESP_FAIL;
#ifdef FILE_SYSTEM_SUPPORTS_LITTLEFS
    if (_localFsType == LOCAL_FS_LITTLEFS)
        ret = esp_littlefs_format(_fsPartitionName.c_str());
    else
#endif
        ret = esp_spiffs_format(NULL);
    // enableCore0WDT();
    Raft::setJsonBoolResult("reformat", respStr, ret == ESP_OK);
    LOG_W(MODULE_PREFIX, "Reformat result %s", (ret == ESP_OK ? "OK" : "FAIL"));
#else

    Raft::setJsonBoolResult("reformat", respStr, true);
    LOG_W(MODULE_PREFIX, "Reformat result %s", "OK");

#endif

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get path of root of default file system
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String FileSystem::getDefaultFSRoot() const
{
    // Default file system root path
    if (_sdFsCache.isUsed)
    {
        if (_defaultToSDIfAvailable)
            return SD_FILE_SYSTEM_NAME;
        if (_localFsCache.fsName.length() == 0)
            return SD_FILE_SYSTEM_NAME;
    }
    if (_localFsCache.fsName.length() == 0)
        return LOCAL_FILE_SYSTEM_NAME;
    return _localFsCache.fsName.c_str();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get file info
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::getFileInfo(const String& fileSystemStr, const String& filename, uint32_t& fileLength)
{
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS)) {
        LOG_W(MODULE_PREFIX, "getFileInfo %s invalid file system %s", filename.c_str(), fileSystemStr.c_str());
        return false;
    }

    // Take mutex
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    // Check file exists
    struct stat st;
    String rootFilename = getFilePath(nameOfFS, filename);

    if (stat(rootFilename.c_str(), &st) != 0)
    {
        RaftMutex_unlock(_fileSysMutex);
#ifdef DEBUG_FILE_NOT_FOUND
        LOG_I(MODULE_PREFIX, "getFileInfo %s cannot stat", rootFilename.c_str());
#endif
        return false;
    }
    if (!S_ISREG(st.st_mode))
    {
        RaftMutex_unlock(_fileSysMutex);
#ifdef WARN_ON_FILE_SYSTEM_ERRORS
        LOG_W(MODULE_PREFIX, "getFileInfo %s is a folder", rootFilename.c_str());
#endif
        return false;
    }
    fileLength = st.st_size;
    RaftMutex_unlock(_fileSysMutex);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get file list JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::getFilesJSON(const char* req, const String& fileSystemStr, const String& folderStr, String& respStr)
{
    // Check file system supported
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
        LOG_W(MODULE_PREFIX, "getFilesJSON unknownFS %s", fileSystemStr.c_str());
        String errMsg = "unknownfs " + fileSystemStr;
        Raft::setJsonErrorResult(req, respStr, errMsg.c_str());
        return false;
    }

    // Check if cached information can be used
    CachedFileSystem& cachedFs = nameOfFS.equalsIgnoreCase(LOCAL_FILE_SYSTEM_NAME) ? _localFsCache : _sdFsCache;
#if !defined(__linux__)
    if (cachedFs.isUsed && _cacheFileSystemInfo && ((folderStr.length() == 0) || (folderStr.equalsIgnoreCase("/"))))
    {
        LOG_I(MODULE_PREFIX, "getFilesJSON using cached info");
        return fileInfoCacheToJSON(req, cachedFs, "/", respStr);
    }
#endif

    // Generate info immediately
    return fileInfoGenImmediate(req, cachedFs, folderStr, respStr);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get file contents
// If non-null pointer returned then it must be freed by caller
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint8_t* FileSystem::getFileContents(const String& fileSystemStr, const String& filename, 
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
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
        return nullptr;

    // Get file info - to check length
    struct stat st;
    if (stat(rootFilename.c_str(), &st) != 0)
    {
        RaftMutex_unlock(_fileSysMutex);
#ifdef WARN_ON_FILE_NOT_FOUND
        LOG_W(MODULE_PREFIX, "getContents %s cannot stat", rootFilename.c_str());
#endif
        return nullptr;
    }
    if (!S_ISREG(st.st_mode))
    {
        RaftMutex_unlock(_fileSysMutex);
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
    if (st.st_size >= maxLen-1)
    {
        RaftMutex_unlock(_fileSysMutex);
#ifdef WARN_ON_FILE_TOO_BIG
        LOG_W(MODULE_PREFIX, "getContents %s free heap %d size %d too big to read", rootFilename.c_str(), maxLen, (int)st.st_size);
#endif
        return nullptr;
    }
    int fileSize = st.st_size;

    // Open file
    FILE* pFile = fopen(rootFilename.c_str(), "rb");
    if (!pFile)
    {
        RaftMutex_unlock(_fileSysMutex);
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
    uint8_t* pBuf = allocator.allocate(fileSize+1);
    if (!pBuf)
    {
        fclose(pFile);
        RaftMutex_unlock(_fileSysMutex);
#ifdef WARN_ON_FILE_TOO_BIG
        LOG_W(MODULE_PREFIX, "getContents failed to allocate %d", fileSize);
#endif
        return nullptr;
    }

    // Read
    size_t bytesRead = fread(pBuf, 1, fileSize, pFile);
    fclose(pFile);
    RaftMutex_unlock(_fileSysMutex);
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

bool FileSystem::setFileContents(const String& fileSystemStr, const String& filename, String& fileContents)
{
    // Check file system supported
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
        return false;
    }

    // Take mutex
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    // Open file for writing
    String rootFilename = getFilePath(nameOfFS, filename);
    FILE* pFile = fopen(rootFilename.c_str(), "wb");
    if (!pFile)
    {
        RaftMutex_unlock(_fileSysMutex);
#ifdef WARN_ON_FILE_SYSTEM_ERRORS
        LOG_W(MODULE_PREFIX, "setContents failed to open file to write %s", rootFilename.c_str());
#endif
        return "";
    }

    // Write
    size_t bytesWritten = fwrite((uint8_t*)(fileContents.c_str()), 1, fileContents.length(), pFile);
    fclose(pFile);

    // Clean up
#ifdef DEBUG_CACHE_FS_INFO
    LOG_I(MODULE_PREFIX, "setFileContents cache invalid");
#endif
#if !defined(__linux__)
    markFileCacheDirty(nameOfFS, filename);
#endif
    RaftMutex_unlock(_fileSysMutex);
    return bytesWritten == fileContents.length();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Delete
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::deleteFile(const String& fileSystemStr, const String& filename)
{
    // Check file system supported
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
        return false;
    }
    
    // Take mutex
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
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
#if !defined(__linux__)
    markFileCacheDirty(nameOfFS, filename);
#endif
    RaftMutex_unlock(_fileSysMutex);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Read line
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

char* FileSystem::readLineFromFile(char* pBuf, int maxLen, FILE* pFile)
{
    // Iterate over chars
    pBuf[0] = 0;
    char* pCurPtr = pBuf;
    int curLen = 0;
    while (true)
    {
        if (curLen >= maxLen-1)
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
String FileSystem::readLineFromFile(FILE* pFile, int maxLen)
{
    // Build the string
    String lineStr;
    int curLen = 0;
    while (true)
    {
        if (curLen >= maxLen-1)
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

String FileSystem::getFileExtension(const String& fileName)
{
    String extn;
    // Find last .
    int dotPos = fileName.lastIndexOf('.');
    if (dotPos < 0)
        return extn;
    // Return substring
    return fileName.substring(dotPos+1);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get file system and check ok
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::checkFileSystem(const String& fileSystemStr, String& fsName) const
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

String FileSystem::getFilePath(const String& nameOfFS, const String& filename) const
{
    // Check if filename already contains file system
    if ((filename.indexOf(LOCAL_FILE_SYSTEM_PATH_ELEMENT) >= 0) || (filename.indexOf(SD_FILE_SYSTEM_PATH_ELEMENT) >= 0))
        return (filename.startsWith("/") ? filename : ("/" + filename));
    
#ifdef __linux__
    // On Linux, if the filename is an absolute path (starts with /) and doesn't contain
    // a virtual filesystem marker, treat it as a real filesystem path
    if (filename.startsWith("/"))
        return filename;
#endif
    
    return (filename.startsWith("/") ? "/" + nameOfFS + filename : ("/" + nameOfFS + "/" + filename));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get file path with fs check
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::getFileFullPath(const String& filename, String& fileFullPath) const
{
    // Extract filename elements
    String modFileName = filename;
    String fsName;
    modFileName.trim();
    int firstSlashPos = modFileName.indexOf('/');
    if (firstSlashPos > 0)
    {
        fsName = modFileName.substring(0, firstSlashPos);
        modFileName = modFileName.substring(firstSlashPos+1);
    }

    // Check the file system is valid
    String nameOfFS;
    if (!checkFileSystem(fsName, nameOfFS)) {
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

bool FileSystem::exists(const char* path) const
{
    // Take mutex
#ifdef DEBUG_FILE_EXISTS_PERFORMANCE
    uint64_t st1 = micros();
#endif
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;
#ifdef DEBUG_FILE_EXISTS_PERFORMANCE
    uint64_t st2 = micros();
#endif
    struct stat buffer;   
    bool rslt = (stat(path, &buffer) == 0);
#ifdef DEBUG_FILE_EXISTS_PERFORMANCE
    uint64_t st3 = micros();
#endif
    RaftMutex_unlock(_fileSysMutex);
#ifdef DEBUG_FILE_EXISTS_PERFORMANCE
    uint64_t st4 = micros();
    LOG_I(MODULE_PREFIX, "exists 1:%lld 2:%lld 3:%lld", st2-st1, st3-st2, st4-st3);
#endif
    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Path type - folder/file/non-existent
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileSystem::FileSystemStatType FileSystem::pathType(const char* filename)
{
    // Take mutex
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
        return FILE_SYSTEM_STAT_NO_EXIST;
    struct stat buffer;
    bool rslt = stat(filename, &buffer);
    RaftMutex_unlock(_fileSysMutex);
    if (rslt != 0)
        return FILE_SYSTEM_STAT_NO_EXIST;
    if (S_ISREG(buffer.st_mode))
        return FILE_SYSTEM_STAT_FILE;
    return FILE_SYSTEM_STAT_DIR;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get a section of a file
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::getFileSection(const String& fileSystemStr, const String& filename, uint32_t sectionStart, uint8_t* pBuf, 
            uint32_t sectionLen, uint32_t& readLen)
{
    // Check file system supported
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
        LOG_W(MODULE_PREFIX, "getFileSection %s invalid file system %s", filename.c_str(), fileSystemStr.c_str());
        return false;
    }

    // Take mutex
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    // Open file
    String rootFilename = getFilePath(nameOfFS, filename);
    FILE* pFile = fopen(rootFilename.c_str(), "rb");
    if (!pFile)
    {
        RaftMutex_unlock(_fileSysMutex);
        LOG_W(MODULE_PREFIX, "getFileSection failed to open file to read %s", rootFilename.c_str());
        return false;
    }

    // Move to appropriate place in file
    fseek(pFile, sectionStart, SEEK_SET);

    // Read
    readLen = fread((char*)pBuf, 1, sectionLen, pFile);
    fclose(pFile);
    RaftMutex_unlock(_fileSysMutex);
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
SpiramAwareUint8Vector FileSystem::getFileSection(const String& fileSystemStr, const String& filename, uint32_t sectionStart, uint32_t sectionLen)
{
    // Check file system supported
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
        LOG_W(MODULE_PREFIX, "getFileSection %s invalid file system %s", filename.c_str(), fileSystemStr.c_str());
        return SpiramAwareUint8Vector();
    }

    // Take mutex
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
        return SpiramAwareUint8Vector();

    // Open file
    String rootFilename = getFilePath(nameOfFS, filename);
    FILE* pFile = fopen(rootFilename.c_str(), "rb");
    if (!pFile)
    {
        RaftMutex_unlock(_fileSysMutex);
        LOG_W(MODULE_PREFIX, "getFileSection failed to open file to read %s", rootFilename.c_str());
        return SpiramAwareUint8Vector();
    }

    // Move to appropriate place in file
    fseek(pFile, sectionStart, SEEK_SET);

    // Read
    SpiramAwareUint8Vector fileData;
    fileData.resize(sectionLen);
    int readLen = fread((char*)fileData.data(), 1, fileData.size(), pFile);
    fclose(pFile);
    RaftMutex_unlock(_fileSysMutex);

    // Return data
    if (readLen <= 0)
        return SpiramAwareUint8Vector();
    else if (readLen < (int)fileData.size())
        fileData.resize(readLen);
    return fileData;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get a line from a text file
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::getFileLine(const String& fileSystemStr, const String& filename, uint32_t startFilePos, uint8_t* pBuf, 
            uint32_t lineMaxLen, uint32_t& fileCurPos)
{
    // Check file system supported
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
        LOG_W(MODULE_PREFIX, "getFileLine %s invalid file system %s", filename.c_str(), fileSystemStr.c_str());
        return false;
    }

    // Take mutex
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    // Open file for text reading
    String rootFilename = getFilePath(nameOfFS, filename);
    FILE* pFile = fopen(rootFilename.c_str(), "r");
    if (!pFile)
    {
        RaftMutex_unlock(_fileSysMutex);
        LOG_W(MODULE_PREFIX, "getFileLine failed to open file to read %s", rootFilename.c_str());
        return false;
    }

    // Move to appropriate place in file
    fseek(pFile, startFilePos, SEEK_SET);

    // Read line
    char* pReadLine = readLineFromFile((char*)pBuf, lineMaxLen-1, pFile);

    // Record final
    fileCurPos = ftell(pFile);

    // Close
    fclose(pFile);
    RaftMutex_unlock(_fileSysMutex);

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
String FileSystem::getFileLine(const String& fileSystemStr, const String& filename, uint32_t startFilePos, uint32_t lineMaxLen, uint32_t& fileCurPos)
{
    // Check file system supported
    String nameOfFS;
    if (!checkFileSystem(fileSystemStr, nameOfFS))
    {
        LOG_W(MODULE_PREFIX, "getFileLine %s invalid file system %s", filename.c_str(), fileSystemStr.c_str());
        return "";
    }

    // Take mutex
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
        return "";

    // Open file for text reading
    String rootFilename = getFilePath(nameOfFS, filename);
    FILE* pFile = fopen(rootFilename.c_str(), "r");
    if (!pFile)
    {
        RaftMutex_unlock(_fileSysMutex);
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
    RaftMutex_unlock(_fileSysMutex);

    // Return line
    return line;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Open file
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FILE* FileSystem::fileOpen(const String& fileSystemStr, const String& filename, 
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
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
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

    FILE* pFile = fopen(rootFilename.c_str(), writeMode ? ((seekToPos != 0) || seekFromEnd ? "ab" : "wb") : "rb");

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
    RaftMutex_unlock(_fileSysMutex);

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

bool FileSystem::fileClose(FILE* pFile, const String& fileSystemStr, const String& filename, bool fileModified)
{
    // Check file system supported
    String nameOfFS;
    checkFileSystem(fileSystemStr, nameOfFS);

    // Take mutex
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    // Check if file modified
    if (fileModified)
    {
#if !defined(__linux__)
        markFileCacheDirty(nameOfFS, filename);
#endif
    }
    
    // Close file
    fclose(pFile);

    // Release mutex
    RaftMutex_unlock(_fileSysMutex);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Read from file
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t FileSystem::fileRead(FILE* pFile, uint8_t* pBuf, uint32_t readLen)
{
    // Ensure valid
    if (!pFile)
    {
        LOG_W(MODULE_PREFIX, "fileRead filePtr null");
        return 0;
    }

    // Take mutex
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;

    // Read
    uint32_t lenRead = fread((char*)pBuf, 1, readLen, pFile);

    // Release mutex
    RaftMutex_unlock(_fileSysMutex);
    return lenRead;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Read from file
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SpiramAwareUint8Vector FileSystem::fileRead(FILE* pFile, uint32_t readLen)
{
    // Ensure valid
    if (!pFile)
    {
        LOG_W(MODULE_PREFIX, "fileRead filePtr null");
        return SpiramAwareUint8Vector();
    }

    // Take mutex
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
        return SpiramAwareUint8Vector();

    // Read
    SpiramAwareUint8Vector fileData;
    fileData.resize(readLen);
    uint32_t lenRead = fread((char*)fileData.data(), 1, fileData.size(), pFile);

    // Release mutex
    RaftMutex_unlock(_fileSysMutex);

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

uint32_t FileSystem::fileWrite(FILE* pFile, const uint8_t* pBuf, uint32_t writeLen)
{
    // Ensure valid
    if (!pFile)
    {
        LOG_W(MODULE_PREFIX, "fileWrite filePtr null");
        return 0;
    }

    // Take mutex
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;

    // Read
    uint32_t lenWritten = fwrite((char*)pBuf, 1, writeLen, pFile);

    // Release mutex
    RaftMutex_unlock(_fileSysMutex);
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

uint32_t FileSystem::filePos(FILE* pFile)
{
    // Ensure valid
    if (!pFile)
    {
        LOG_W(MODULE_PREFIX, "filePos filePtr null");
        return 0;
    }

    // Take mutex
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;

    // Read
    uint32_t filePosition = ftell(pFile);

    // Release mutex
    RaftMutex_unlock(_fileSysMutex);
    return filePosition;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Seek file
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::fileSeek(FILE* pFile, uint32_t seekPos)
{
    // Ensure valid
    if (!pFile)
    {
        LOG_W(MODULE_PREFIX, "fileSeek filePtr null");
        return false;
    }

    // Take mutex
    if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    // Seek
    fseek(pFile, seekPos, SEEK_SET);

    // Release mutex
    RaftMutex_unlock(_fileSysMutex);
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

#if defined(__linux__)

#ifdef FILE_SYSTEM_SUPPORTS_LITTLEFS
bool FileSystem::localFileSystemSetupLittleFS(bool formatIfCorrupt)
{
    // Linux uses same directory-based approach for both SPIFFS and LittleFS
    return localFileSystemSetupSPIFFS(formatIfCorrupt);
}
#endif

#else  // ESP32 version

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

#endif  // __linux__ / ESP32 LittleFS setup

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup local file system - SPIFFS
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(__linux__)

bool FileSystem::localFileSystemSetupSPIFFS(bool formatIfCorrupt)
{
    // Create directory for local file system simulation
    mkdir("/tmp/sandbot_local", 0755);
    _localFsType = LOCAL_FS_SPIFFS;
    _localFsCache.isUsed = true;
    _localFsCache.fsName = LOCAL_FILE_SYSTEM_NAME;
    _localFsCache.fsBase = "/tmp/sandbot_local";
    _localFsCache.fsSizeBytes = 1024 * 1024 * 100; // 100MB
    _localFsCache.fsUsedBytes = 0;
    _localFsCache.isSizeInfoValid = true;
    LOG_I(MODULE_PREFIX, "localFileSystemSetup Linux directory /tmp/sandbot_local created");
    return true;
}

#else  // ESP32 version

bool FileSystem::localFileSystemSetupSPIFFS(bool formatIfCorrupt)
{
    // Set partition name
    _fsPartitionName = LOCAL_FILE_SYSTEM_PARTITION_LABEL;

    // Using ESP IDF virtual file system
    esp_vfs_spiffs_conf_t conf = {
        .base_path = LOCAL_FILE_SYSTEM_BASE_PATH,
        .partition_label = _fsPartitionName.c_str(),
        .max_files = 5,
        .format_if_mount_failed = formatIfCorrupt
    };        
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

#endif  // __linux__ / ESP32 SPIFFS setup

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup SD file system
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(__linux__)

bool FileSystem::sdFileSystemSetup(bool enableSD, int sdMOSIPin, int sdMISOPin, int sdCLKPin, int sdCSPin)
{
    if (!enableSD)
    {
        LOG_I(MODULE_PREFIX, "sdFileSystemSetup SD disabled");
        return false;
    }

    // Create directory for SD card simulation
    mkdir("/tmp/sandbot_sd", 0755);
    _sdFsCache.isUsed = true;
    _sdFsCache.fsName = SD_FILE_SYSTEM_NAME;
    _sdFsCache.fsBase = "/tmp/sandbot_sd";
    _sdFsCache.fsSizeBytes = 1024 * 1024 * 1000; // 1GB
    _sdFsCache.fsUsedBytes = 0;
    _sdFsCache.isSizeInfoValid = true;
    LOG_I(MODULE_PREFIX, "sdFileSystemSetup Linux directory /tmp/sandbot_sd created");
    return true;
}

#else  // ESP32 version

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
    sdmmc_card_t* pCard = nullptr;

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

        .intr_flags = 0
    };
    esp_err_t ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
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
    dev_config.gpio_sck  = (gpio_num_t)sdCLKPin;
    dev_config.gpio_cs   = (gpio_num_t)sdCSPin;

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

#endif  // __linux__ / ESP32 SD setup

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convert cached file system info to JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if !defined(__linux__)

bool FileSystem::fileInfoCacheToJSON(const char* req, CachedFileSystem& cachedFs, const String& folderStr, String& respStr)
{
    // Check valid
    if (cachedFs.isSizeInfoValid && cachedFs.isFileInfoValid)
    {
        LOG_I(MODULE_PREFIX, "fileInfoCacheToJSON cached info valid");

        uint32_t debugStartMs = millis();
        String fileListStr;
        bool firstFile = true;
        uint32_t fileCount = 0;
        for (CachedFileInfo& cachedFileInfo : cachedFs.cachedRootFileList)
        {
            fileListStr += String(firstFile ? R"({"name":")" : R"(,{"name":")") + cachedFileInfo.fileName.c_str() + R"(","size":)" + String(cachedFileInfo.fileSize) + "}";
            firstFile = false;
            fileCount++;
        }
        // Format response
        respStr = formatJSONFileInfo(req, cachedFs, fileListStr, folderStr);
        uint32_t elapsedMs = millis() - debugStartMs;
        LOG_I(MODULE_PREFIX, "fileInfoCacheToJSON elapsed %dms fileCount %d", elapsedMs, fileCount);
        return true;
    }

    // Info invalid - need to wait
    LOG_I(MODULE_PREFIX, "fileInfoCacheToJSON cached info invalid - need to wait");
    Raft::setJsonErrorResult(req, respStr, "fsinfodirty");
    return false;
}

#endif  // !defined(__linux__) - ESP32-only cache function

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get JSON file info immediately
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystem::fileInfoGenImmediate(const char* req, CachedFileSystem& cachedFs, const String& folderStr, String& respStr)
{
    // Check if file-system info is valid
    if (!cachedFs.isSizeInfoValid)
    {
        if (!fileSysInfoUpdateCache(req, cachedFs, respStr))
            return false;
    }

    // Take mutex
    if (!RaftMutex_lock(_fileSysMutex, 0))
    {
        Raft::setJsonErrorResult(req, respStr, "fsbusy");
        return false;
    }

    // Debug
    uint32_t debugStartMs = millis();

    // Check file system is valid
    if (cachedFs.fsSizeBytes == 0)
    {
        RaftMutex_unlock(_fileSysMutex);
        LOG_W(MODULE_PREFIX, "getFilesJSON No valid file system");
        Raft::setJsonErrorResult(req, respStr, "nofs");
        return false;
    }

    // Open directory
    String rootFolder = cachedFs.fsBase.c_str();
    if (!folderStr.startsWith("/"))
        rootFolder += "/";
    rootFolder += folderStr;
    DIR* dir = opendir(rootFolder.c_str());
    if (!dir)
    {
        RaftMutex_unlock(_fileSysMutex);
        LOG_W(MODULE_PREFIX, "getFilesJSON Failed to open base folder %s", rootFolder.c_str());
        Raft::setJsonErrorResult(req, respStr, "nofolder");
        return false;
    }

    // Read directory entries
    bool firstFile = true;
    String fileListStr;
    struct dirent* ent = NULL;
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
        String filePath = (rootFolder.endsWith("/") ? rootFolder + fName : rootFolder + "/" + fName);
        if (stat(filePath.c_str(), &st) == 0) 
        {
            fileSize = st.st_size;
        }

        // Form the JSON list
        if (!firstFile)
            fileListStr += ",";
        firstFile = false;
        fileListStr += R"({"name":")";
        fileListStr += ent->d_name;
        fileListStr += R"(","size":)";
        fileListStr += String(fileSize);
        fileListStr += "}";
    }

    // Finished with file list
    closedir(dir);
    RaftMutex_unlock(_fileSysMutex);

    // Format response
    respStr = formatJSONFileInfo(req, cachedFs, fileListStr, rootFolder);

    // Debug
    uint32_t debugGetFilesMs = millis() - debugStartMs;
    LOG_I(MODULE_PREFIX, "getFilesJSON timing fileList %dms", debugGetFilesMs);

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Update File system info cache
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(__linux__)

bool FileSystem::fileSysInfoUpdateCache(const char* req, CachedFileSystem& cachedFs, String& respStr)
{
    // On Linux, size info is already set during setup
    if (cachedFs.isSizeInfoValid)
        return true;

    LOG_W(MODULE_PREFIX, "fileSysInfoUpdateCache size info not valid on Linux");
    return false;
}

#else  // ESP32 version

bool FileSystem::fileSysInfoUpdateCache(const char* req, CachedFileSystem& cachedFs, String& respStr)
{
    // Take mutex
    if (!RaftMutex_lock(_fileSysMutex, 0))
    {
        Raft::setJsonErrorResult(req, respStr, "fsbusy");
        return false;
    }

    // Get size of file systems
    uint32_t debugStartMs = millis();
    String fsName = cachedFs.fsName.c_str();
    if (fsName == LOCAL_FILE_SYSTEM_NAME)
    {
        size_t sizeBytes = 0, usedBytes = 0;
        esp_err_t ret = ESP_FAIL;
#ifdef FILE_SYSTEM_SUPPORTS_LITTLEFS
        if (_localFsType == LOCAL_FS_LITTLEFS)
            ret = esp_littlefs_info(_fsPartitionName.c_str(), &sizeBytes, &usedBytes);
        else
#endif
            ret = esp_spiffs_info(_fsPartitionName.c_str(), &sizeBytes, &usedBytes);
        if (ret != ESP_OK)
        {
            RaftMutex_unlock(_fileSysMutex);
            LOG_W(MODULE_PREFIX, "fileSysInfoUpdateCache failed to get file system info (error %s)", esp_err_to_name(ret));
            Raft::setJsonErrorResult(req, respStr, "fsInfo");
            return false;
        }
        // FS settings
        cachedFs.fsSizeBytes = sizeBytes;
        cachedFs.fsUsedBytes = usedBytes;
        cachedFs.isSizeInfoValid = true;
    }
    else if (fsName == SD_FILE_SYSTEM_NAME)
    {
        // Get size info
        sdmmc_card_t* pCard = (sdmmc_card_t*)_pSDCard;
        if (pCard)
        {
            cachedFs.fsSizeBytes = ((double) pCard->csd.capacity) * pCard->csd.sector_size;
        	FATFS* fsinfo;
            DWORD fre_clust;
            if(f_getfree("0:",&fre_clust,&fsinfo) == 0)
            {
                cachedFs.fsUsedBytes = ((double)(fsinfo->csize))*((fsinfo->n_fatent - 2) - (fsinfo->free_clst))
            #if _MAX_SS != 512
                    *(fsinfo->ssize);
            #else
                    *512;
            #endif
            }
            cachedFs.isSizeInfoValid = true;
        }
    }
    RaftMutex_unlock(_fileSysMutex);
    uint32_t debugGetFsInfoMs = millis() - debugStartMs;
    LOG_I(MODULE_PREFIX, "fileSysInfoUpdateCache timing fsInfo %dms", debugGetFsInfoMs);
    return true;
}

#endif  // __linux__ / ESP32 fileSysInfoUpdateCache

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mark file cache dirty
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if !defined(__linux__)

void FileSystem::markFileCacheDirty(const String& fsName, const String& filename)
{
    // Get file system info
    CachedFileSystem& cachedFs = fsName.equalsIgnoreCase(LOCAL_FILE_SYSTEM_NAME) ? _localFsCache : _sdFsCache;

    // Set FS info invalid
    cachedFs.isFileInfoValid = false;
    cachedFs.isSizeInfoValid = false;

    // Check caching enabled
    if (!_cacheFileSystemInfo)
        return;

    // Check valid
    if (!cachedFs.isFileInfoSetup)
        return;

    // Find the file in the cached file list
    bool fileFound = false;
    for (CachedFileInfo& cachedFileInfo : cachedFs.cachedRootFileList)
    {
        // Check name
        if (cachedFileInfo.fileName.compare(filename.c_str()) == 0)
        {
            // Invalidate specific file
            cachedFileInfo.isValid = false;
            fileFound = true;
            break;
        }
    }

    // Check for file not found
    if (!fileFound)
    {
        CachedFileInfo newFileInfo;
        newFileInfo.fileName = filename.c_str();
        newFileInfo.fileSize = 0;
        newFileInfo.isValid = false;
        cachedFs.cachedRootFileList.push_back(newFileInfo);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File system cache service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileSystem::fileSystemCacheService(CachedFileSystem& cachedFs)
{
    // Check if file-system info is invalid
    if (!cachedFs.isUsed)
        return;

    // Update fsInfo if required        
    String respStr;
    if (!cachedFs.isSizeInfoValid)
    {
        fileSysInfoUpdateCache("", cachedFs, respStr);
        return;
    }

    // Update file info if required
    if (!cachedFs.isFileInfoSetup)
    {
        uint32_t debugStartMs = millis();

        // Take mutex
        if (!RaftMutex_lock(_fileSysMutex, RAFT_MUTEX_WAIT_FOREVER))
            return;

        // Clear file list
        cachedFs.cachedRootFileList.clear();

        // Iterate file info
        String rootFolder = (cachedFs.fsBase + "/").c_str();
        DIR* dir = opendir(rootFolder.c_str());
        if (!dir)
        {
            RaftMutex_unlock(_fileSysMutex);
            return;
        }
        // Read directory entries
        struct dirent* ent = NULL;
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
        cachedFs.isFileInfoSetup = true;
        cachedFs.isFileInfoValid = true;
        RaftMutex_unlock(_fileSysMutex);
        uint32_t debugCachedFsSetupMs = millis() - debugStartMs;
        LOG_I(MODULE_PREFIX, "fileSystemCacheService fs %s files %d took %dms", 
                    cachedFs.fsName.c_str(), fileCount, debugCachedFsSetupMs);
        return;
    }

    // Check file info
    if (cachedFs.isFileInfoSetup && !cachedFs.isFileInfoValid)
    {
        // Update info for specific file(s)
        // Take mutex
        if (!RaftMutex_lock(_fileSysMutex, 0))
            return;
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
                    LOG_I(MODULE_PREFIX, "fileSystemCacheService updated %s size %d", 
                            it->fileName.c_str(), (int)st.st_size);
                }
                else
                {
                    // Remove from list
                    cachedFs.cachedRootFileList.erase(it);
                    LOG_I(MODULE_PREFIX, "fileSystemCacheService deleted %s", 
                            it->fileName.c_str());
                    allValid = false;
                    break;
                }
                // File info now valid
                it->isValid = true;
                break;
            }
        }
        LOG_I(MODULE_PREFIX, "fileSystemCacheService fileInfo %s", allValid ? "valid" : "invalid");
        cachedFs.isFileInfoValid = allValid;
        RaftMutex_unlock(_fileSysMutex);
    }
}

#endif  // !defined(__linux__) - ESP32-only cache functions

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Format JSON file info
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String FileSystem::formatJSONFileInfo(const char* req, CachedFileSystem& cachedFs, 
            const String& fileListStr, const String& rootFolder)
{
    // Start response JSON
    return R"({"req":")" + String(req) + R"(","rslt":"ok","fsName":")" + String(cachedFs.fsName.c_str()) + 
                R"(","fsBase":")" + String(cachedFs.fsBase.c_str()) + 
                R"(","diskSize":)" + String(cachedFs.fsSizeBytes) + R"(,"diskUsed":)" + String(cachedFs.fsUsedBytes) +
                R"(,"folder":")" + rootFolder + R"(","files":[)" + fileListStr + "]}";
}
