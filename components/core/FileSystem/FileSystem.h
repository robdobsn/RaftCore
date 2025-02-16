/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FileSystem
// Handles SPIFFS/LittleFS and SD card file access
//
// Rob Dobson 2018-2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include <string>
#include "RaftUtils.h"
#include "RaftThreading.h"
#include "FileStreamBlock.h"
#include "SpiramAwareAllocator.h"

#define FILE_SYSTEM_SUPPORTS_LITTLEFS

class FileSystem
{
public:
    /// @brief Constructor
    FileSystem();

    /// @brief Destructor
    virtual ~FileSystem();

    /// @brief Local file system types
    /// @note LOCAL_FS_DISABLE - no local file system
    /// @note LOCAL_FS_SPIFFS - SPIFFS file system
    /// @note LOCAL_FS_LITTLEFS - LittleFS file system
    typedef enum {
        LOCAL_FS_DISABLE,
        LOCAL_FS_SPIFFS,
        LOCAL_FS_LITTLEFS,
    } LocalFileSystemType;

    /// @brief Setup file system
    /// @param localFsType Local file system type
    /// @param localFsFormatIfCorrupt Format if corrupt
    /// @param enableSD Enable SD card
    /// @param sdMOSIPin MOSI pin for SD card
    /// @param sdMISOPin MISO pin for SD card
    /// @param sdCLKPin CLK pin for SD card
    /// @param sdCSPin CS pin for SD card
    /// @param defaultToSDIfAvailable Default to SD if it is available (otherwise default to local FS)
    /// @param cacheFileSystemInfo Cache file system info
    void setup(LocalFileSystemType localFsType, bool localFsFormatIfCorrupt, bool enableSD, 
        int sdMOSIPin, int sdMISOPin, int sdCLKPin, int sdCSPin, bool defaultToSDIfAvailable,
        bool cacheFileSystemInfo);

    /// @brief Loop (called from main loop)
    void loop();

    /// @brief Reformat the file system
    /// @param fileSystemStr File system string
    /// @param respStr Response string
    /// @param force Force reformat
    /// @return true if successful
    bool reformat(const String& fileSystemStr, String& respStr, bool force);

    /// @brief Get path of root of default file system
    /// @return Path of root of default file system
    String getDefaultFSRoot() const;

    /// @brief Get a list of files on the file system as a JSON format string
    /// @param req Request string
    /// @param fileSystemStr File system string
    /// @param folderStr Folder string
    /// @param respStr String to receive response
    /// @return Return code
    /// @note Format of JSON is {"req":"filelist","rslt":"ok","fsName":"local","fsBase":"/local","diskSize":524288,"diskUsed":73728,"folder":"/local/","files":[{"name":".built","size":0},{"name":"index.774a82d4.js.gz","size":54676},{"name":"index.a0238169.css.gz","size":1566},{"name":"index.html.gz","size":3605}]}
    RaftRetCode getFilesJSON(const char* req, const String& fileSystemStr, const String& folderStr, SpiramAwareString& respStr);

    /// @brief Get file contents as a string
    /// @param fileSystemStr File system string
    /// @param filename Filename
    /// @param maxLen Maximum length of file to read
    /// @param isTruncated true if the file was truncated
    /// @return File contents as a string pointer - if a non-null pointer is returned then it must be freed by caller
    [[deprecated("Use SpiramAwareUint8Vector getFileContents() instead")]]
    uint8_t* getFileContents(const String& fileSystemStr, const String& filename, int maxLen=0);

    /// @brief Get file contents
    /// @param fileSystemStr File system string
    /// @param filename Filename
    /// @param maxLen Maximum length of file to read
    /// @param fileBuf Buffer to read into
    /// @param isTruncated true if the file was truncated
    /// @return Return code
    RaftRetCode getFileContents(const String& fileSystemStr, const String& filename, int maxLen, SpiramAwareUint8Vector& fileBuf, bool& isTruncated);

    /// @brief Set file contents
    /// @param fileSystemStr File system string
    /// @param filename Filename
    /// @param fileContents File contents
    /// @return true if successful
    bool setFileContents(const String& fileSystemStr, const String& filename, String& fileContents);

    /// @brief Set file contents
    /// @param fileSystemStr File system string
    /// @param filename Filename
    /// @param pBuf Buffer
    /// @param bufLen Length of buffer
    /// @return true if successful
    bool setFileContents(const String& fileSystemStr, const String& filename, const uint8_t* pBuf, uint32_t bufLen);

    /// @brief Delete file on file system
    /// @param fileSystemStr File system string
    /// @param filename Filename
    /// @return true if successful
    bool deleteFile(const String& fileSystemStr, const String& filename);
    
    /// @brief Test file exists and get info
    /// @param fileSystemStr File system string
    /// @param filename Filename
    /// @param fileLength Length of file
    /// @return true if file exists
    bool getFileInfo(const String& fileSystemStr, const String& filename, uint32_t& fileLength);

    /// @brief Get file name extension
    /// @param fileName Filename
    /// @return File extension
    static String getFileExtension(const String& filename);

    // Read line from file
    String readLineFromFile(FILE* pFile, int maxLen);

    /// @brief Read line from file
    /// @param pFile File pointer
    /// @param maxLen Maximum length of line
    /// @return String containing line
    SpiramAwareString readLineFromFile(FILE* pFile, int maxLen);

    /// @brief Exists - check if file exists
    /// @param path Path to check
    /// @return true if file exists
    bool exists(const char* path) const;

    // Stat (details on a file)
    typedef enum {
        FILE_SYSTEM_STAT_NO_EXIST,
        FILE_SYSTEM_STAT_DIR,
        FILE_SYSTEM_STAT_FILE,
    } FileSystemStatType;

    /// @brief Get stat on a file
    /// @param filename Filename
    /// @return File system stat type
    FileSystemStatType pathType(const char* filename);

    /// @brief Get a file path using default file system if necessary
    /// @param filename Filename
    /// @param fileFullPath Full path to file
    /// @return true if successful
    bool getFileFullPath(const String& filename, String& fileFullPath) const;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get a section of a file
    /// @param fileSystemStr File system string
    /// @param filename Filename
    /// @param sectionStart Start position in file
    /// @param sectionLen Length of section to read
    /// @return true if successful
    SpiramAwareUint8Vector getFileSection(const String& fileSystemStr, const String& filename, uint32_t sectionStart,
                uint32_t sectionLen);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get a line from a text file
    /// @param fileSystemStr File system string
    /// @param filename Filename
    /// @param startFilePos Start position in file
    /// @param pBuf Buffer to read into
    /// @param lineMaxLen Maximum length of line
    /// @param fileCurPos Current position in file
    /// @return true if successful
    String getFileLine(const String& fileSystemStr, const String& filename, uint32_t startFilePos, uint32_t lineMaxLen, uint32_t& fileCurPos);

    /// @brief Get a line from a text file
    /// @param fileSystemStr File system string
    /// @param filename Filename
    /// @param startFilePos Start position in file
    /// @param pBuf Buffer to read into
    /// @param lineMaxLen Maximum length of line
    /// @param fileCurPos Current position in file
    /// @return string containing line
    SpiramAwareString getFileLine(const String& fileSystemStr, const String& filename, uint32_t startFilePos, uint32_t lineMaxLen, uint32_t& fileCurPos);

    /// @brief Open file
    /// @param fileSystemStr File system string
    /// @param filename Filename
    /// @param writeMode Write mode
    /// @param seekToPos Seek to position
    /// @param seekFromEnd Seek from end
    /// @return File pointer
    FILE* fileOpen(const String& fileSystemStr, const String& filename, bool writeMode, uint32_t seekToPos, bool seekFromEnd = false);

    /// @brief Close file
    /// @param pFile File pointer
    /// @param fileSystemStr File system string
    /// @param filename Filename
    /// @param fileModified File modified
    /// @return true if successful
    bool fileClose(FILE* pFile, const String& fileSystemStr, const String& filename, bool fileModified);

    /// @brief Read from file
    /// @param pFile File pointer
    /// @param pBuf Buffer
    /// @param readLen Length to read
    /// @return Number of bytes read
    [[deprecated("Use SpiramAwareUint8Vector fileRead() instead")]]
    uint32_t fileRead(FILE* pFile, uint8_t* pBuf, uint32_t readLen);

    // Read from file
    SpiramAwareUint8Vector fileRead(FILE* pFile, uint32_t readLen);

    // Write to file
    uint32_t fileWrite(FILE* pFile, const uint8_t* pBuf, uint32_t writeLen);

    /// @brief Get file position
    /// @param pFile File pointer
    /// @return File position
    uint32_t filePos(FILE* pFile);

    /// @brief Set file position
    /// @param pFile File pointer
    /// @param seekPos Position to seek to
    /// @return true if successful
    bool fileSeek(FILE* pFile, uint32_t seekPos);
    
    /// @brief Get temporary file name
    /// @return Temporary file name
    String getTempFileName();

    /// @brief Get file system size
    /// @param fileSystemStr File system string
    /// @param fsSizeBytes File system size in bytes
    /// @param fsUsedBytes File system used bytes
    /// @return true if successful
    bool getFileSystemSize(const String& fileSystemStr, uint32_t& fsSizeBytes, uint32_t& fsUsedBytes);
    
private:

    // File system settings
    LocalFileSystemType _localFsType = LOCAL_FS_DISABLE;
    bool _defaultToSDIfAvailable = false;
    bool _sdIsOk = false;
    bool _cacheFileSystemInfo = false;

    // SD card
    void* _pSDCard = nullptr;

    // FS info
    class FileSystemInfo
    {
    public:
        String fsName;
        String fsBase;
        uint32_t fsSizeBytes = 0;
        uint32_t fsUsedBytes = 0;
        bool isSizeInfoValid = false;
        bool isUsed = false;
    };
    FileSystemInfo _sdFsInfo;
    FileSystemInfo _localFsInfo;
    
    // Cached file info
    class CachedFileInfo
    {
    public:
        std::basic_string<char, std::char_traits<char>, SpiramAwareAllocator<char>> fileName;
        uint32_t fileSize = 0;
        bool isValid = false;
    };
    class CachedFileSystem
    {
    public:
        FileSystemInfo fsInfo;
        std::list<CachedFileInfo, SpiramAwareAllocator<CachedFileInfo>> cachedRootFileList;
        bool isFileInfoValid = false;
        bool isFileInfoSetup = false;
    };
    CachedFileSystem _sdFsCache;
    CachedFileSystem _localFsCache;

    // Mutex controlling access to file system
    RaftMutex _fileSysMutex;

    // File system partition name
    String _fsPartitionName;

private:
    /// @brief Check file system integrity
    /// @param fileSystemStr File system string
    /// @param fsName File system name
    /// @return true if successful
    bool checkFileSystem(const String& fileSystemStr, String& fsName) const;

    /// @brief Get file path
    /// @param nameOfFS Name of file system
    /// @param filename Filename
    /// @return File path
    String getFilePath(const String& nameOfFS, const String& filename) const;

    /// @brief Setup local file system
    /// @param formatIfCorrupt Format if corrupt
    void localFileSystemSetup(bool formatIfCorrupt);

#ifdef FILE_SYSTEM_SUPPORTS_LITTLEFS
    /// @brief Setup LittleFS
    /// @param formatIfCorrupt Format if corrupt
    /// @return true if successful
    bool localFileSystemSetupLittleFS(bool formatIfCorrupt);
#endif

    /// @brief Setup SPIFFS
    /// @param formatIfCorrupt Format if corrupt
    /// @return true if successful
    bool localFileSystemSetupSPIFFS(bool formatIfCorrupt);

    /// @brief Setup SD card file system
    /// @param enableSD Enable SD card
    /// @param sdMOSIPin MOSI pin
    /// @param sdMISOPin MISO pin
    /// @param sdCLKPin CLK pin
    /// @param sdCSPin CS pin
    /// @return true if successful
    bool sdFileSystemSetup(bool enableSD, int sdMOSIPin, int sdMISOPin, int sdCLKPin, int sdCSPin);

    /// @brief Get file system cache info in JSON
    /// @param req Request string
    /// @param cachedFs Cached file system
    /// @param folderStr Folder string
    /// @param respBuf Response buffer
    /// @return Return code
    RaftRetCode getFileListJsonFromCache(const char* req, CachedFileSystem& cachedFs, const String& folderStr, SpiramAwareString& respBuf);

    /// @brief Generate file info from file system (no cache)
    /// @param req Request string
    /// @param fsName File system name
    /// @param folderStr Folder string
    /// @param respBuf Response buffer
    /// @return Return code
    RaftRetCode getFileListJson(const char* req, const String& fsName, const String& folderStr, SpiramAwareString& respBuf);

    /// @brief Update file system cache
    /// @param req Request string
    /// @param cachedFs Cached file system
    /// @return Result code
    RaftRetCode cacheUpdate(const char* req, CachedFileSystem& cachedFs);

    /// @brief Mark file cache dirty
    /// @param fsName File system name
    /// @param filename Filename
    void cacheMarkDirty(const String& fsName, const String& filename);

    /// @brief Service file system cache
    /// @param cachedFs Cached file system
    void cacheService(CachedFileSystem& cachedFs);

    /// @brief Format file system info as JSON
    /// @param req Request string
    /// @param fsInfo file system info
    /// @param rootFolder Root folder
    /// @return string containing JSON
    String getFsInfoJson(const char *req, FileSystemInfo &fsInfo, const String &rootFolder);

    /// @brief Number of service calls to prime the cache
    static const uint32_t SERVICE_COUNT_FOR_CACHE_PRIMING = 10;

    /// @brief File system name (generally "local")
    static constexpr const char* LOCAL_FILE_SYSTEM_NAME = "local";

    /// @brief File system base path
    static constexpr const char* LOCAL_FILE_SYSTEM_BASE_PATH = "/local";

    /// @brief File system path element
    static constexpr const char* LOCAL_FILE_SYSTEM_PATH_ELEMENT = "local/";

    /// @brief Local file system alternate name
    static constexpr const char* LOCAL_FILE_SYSTEM_ALT_NAME = "spiffs";

    /// @brief SD file system name
    static constexpr const char* SD_FILE_SYSTEM_NAME = "sd";

    /// @brief SD file system base path
    static constexpr const char* SD_FILE_SYSTEM_BASE_PATH = "/sd";

    /// @brief SD file system path element
    static constexpr const char* SD_FILE_SYSTEM_PATH_ELEMENT = "sd/";

    /// @brief Temporary file name prefix
    static constexpr const char* LOCAL_FILE_SYSTEM_PARTITION_LABEL = "fs";

    /// @brief Temporary file name prefix (alternate)
    static constexpr const char* LOCAL_FILE_SYSTEM_PARTITION_LABEL_ALT = "spiffs";

    // Debug
    static constexpr const char* MODULE_PREFIX = "FileSys";
};

extern FileSystem fileSystem;
