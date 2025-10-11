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
    FileSystem();
    virtual ~FileSystem();

    // Local file system types
    typedef enum {
        LOCAL_FS_DISABLE,
        LOCAL_FS_SPIFFS,
        LOCAL_FS_LITTLEFS,
    } LocalFileSystemType;

    // Setup 
    void setup(LocalFileSystemType localFsType, bool localFsFormatIfCorrupt, bool enableSD, 
        int sdMOSIPin, int sdMISOPin, int sdCLKPin, int sdCSPin, bool defaultToSDIfAvailable,
        bool cacheFileSystemInfo);

    // Service
    void loop();

    // Reformat
    bool reformat(const String& fileSystemStr, String& respStr, bool force);

    // Get path of root of default file system
    String getDefaultFSRoot() const;

    // Get a list of files on the file system as a JSON format string
    // {"rslt":"ok","diskSize":123456,"diskUsed":1234,"folder":"/","files":[{"name":"file1.txt","size":223},{"name":"file2.txt","size":234}]}
    bool getFilesJSON(const char* req, const String& fileSystemStr, const String& folderStr, String& respStr);

    // Get file contents as a string
    // If a non-null pointer is returned then it must be freed by caller
    uint8_t* getFileContents(const String& fileSystemStr, const String& filename, int maxLen=0);

    // Set file contents from string
    bool setFileContents(const String& fileSystemStr, const String& filename, String& fileContents);

    // Delete file on file system
    bool deleteFile(const String& fileSystemStr, const String& filename);
    
    // Test file exists and get info
    bool getFileInfo(const String& fileSystemStr, const String& filename, uint32_t& fileLength);

    // Get file name extension
    static String getFileExtension(const String& filename);

    // Read line from file
    char* readLineFromFile(char* pBuf, int maxLen, FILE* pFile);

    // Read line from file
    String readLineFromFile(FILE* pFile, int maxLen);

    // Exists - check if file exists
    bool exists(const char* path) const;

    // Stat (details on a file)
    typedef enum {
        FILE_SYSTEM_STAT_NO_EXIST,
        FILE_SYSTEM_STAT_DIR,
        FILE_SYSTEM_STAT_FILE,
    } FileSystemStatType;

    // Get stat
    FileSystemStatType pathType(const char* filename);

    // Get a file path using default file system if necessary
    bool getFileFullPath(const String& filename, String& fileFullPath) const;

    // Get a section of a file
    bool getFileSection(const String& fileSystemStr, const String& filename, uint32_t sectionStart, uint8_t* pBuf, 
            uint32_t sectionLen, uint32_t& readLen);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get a section of a file
    /// @param fileSystemStr File system string
    /// @param filename Filename
    /// @param sectionStart Start position in file
    /// @param sectionLen Length of section to read
    /// @return true if successful
    SpiramAwareUint8Vector getFileSection(const String& fileSystemStr, const String& filename, uint32_t sectionStart,
                uint32_t sectionLen);

    // Get a line from a text file
    bool getFileLine(const String& fileSystemStr, const String& filename, uint32_t startFilePos, uint8_t* pBuf, 
            uint32_t lineMaxLen, uint32_t& fileCurPos);

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

    // Open file
    FILE* fileOpen(const String& fileSystemStr, const String& filename, bool writeMode, uint32_t seekToPos, bool seekFromEnd = false);

    // Close file
    bool fileClose(FILE* pFile, const String& fileSystemStr, const String& filename, bool fileModified);

    // Read from file
    uint32_t fileRead(FILE* pFile, uint8_t* pBuf, uint32_t readLen);

    // Read from file
    SpiramAwareUint8Vector fileRead(FILE* pFile, uint32_t readLen);

    // Write to file
    uint32_t fileWrite(FILE* pFile, const uint8_t* pBuf, uint32_t writeLen);

    // Get file position
    uint32_t filePos(FILE* pFile);

    // Set file position
    bool fileSeek(FILE* pFile, uint32_t seekPos);
    
    // Get temporary file name
    String getTempFileName();

    // Get file system size
    bool getFileSystemSize(const String& fileSystemStr, uint32_t& fsSizeBytes, uint32_t& fsUsedBytes);
    
private:

    // File system settings
    LocalFileSystemType _localFsType = LOCAL_FS_DISABLE;
    bool _defaultToSDIfAvailable = false;
    bool _sdIsOk = false;
    bool _cacheFileSystemInfo = false;

    // SD card
    void* _pSDCard = nullptr;

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
        std::basic_string<char, std::char_traits<char>, SpiramAwareAllocator<char>> fsName;
        std::basic_string<char, std::char_traits<char>, SpiramAwareAllocator<char>> fsBase;
        std::list<CachedFileInfo, SpiramAwareAllocator<CachedFileInfo>> cachedRootFileList;
        uint32_t fsSizeBytes = 0;
        uint32_t fsUsedBytes = 0;
        bool isSizeInfoValid = false;
        bool isFileInfoValid = false;
        bool isFileInfoSetup = false;
        bool isUsed = false;
    };
    CachedFileSystem _sdFsCache;
    CachedFileSystem _localFsCache;

    // Mutex controlling access to file system
    mutable RaftMutex _fileSysMutex;

    // File system partition name
    String _fsPartitionName;

private:
    bool checkFileSystem(const String& fileSystemStr, String& fsName) const;
    String getFilePath(const String& nameOfFS, const String& filename) const;
    void localFileSystemSetup(bool formatIfCorrupt);
#ifdef FILE_SYSTEM_SUPPORTS_LITTLEFS
    bool localFileSystemSetupLittleFS(bool formatIfCorrupt);
#endif
    bool localFileSystemSetupSPIFFS(bool formatIfCorrupt);
    bool sdFileSystemSetup(bool enableSD, int sdMOSIPin, int sdMISOPin, int sdCLKPin, int sdCSPin);
    bool fileInfoCacheToJSON(const char* req, CachedFileSystem& cachedFs, const String& folderStr, String& respStr);
    bool fileInfoGenImmediate(const char* req, CachedFileSystem& cachedFs, const String& folderStr, String& respStr);
    bool fileSysInfoUpdateCache(const char* req, CachedFileSystem& cachedFs, String& respStr);
    void markFileCacheDirty(const String& fsName, const String& filename);
    void fileSystemCacheService(CachedFileSystem& cachedFs);
    String formatJSONFileInfo(const char* req, CachedFileSystem& cachedFs, const String& fileListStr, const String& rootFolder);
    static const uint32_t SERVICE_COUNT_FOR_CACHE_PRIMING = 10;

    // File system name
    static constexpr const char* LOCAL_FILE_SYSTEM_NAME = "local";
    static constexpr const char* LOCAL_FILE_SYSTEM_BASE_PATH = "/local";
    static constexpr const char* LOCAL_FILE_SYSTEM_PATH_ELEMENT = "local/";
    static constexpr const char* LOCAL_FILE_SYSTEM_ALT_NAME = "spiffs";
    static constexpr const char* SD_FILE_SYSTEM_NAME = "sd";
    static constexpr const char* SD_FILE_SYSTEM_BASE_PATH = "/sd";
    static constexpr const char* SD_FILE_SYSTEM_PATH_ELEMENT = "sd/";
    static constexpr const char* LOCAL_FILE_SYSTEM_PARTITION_LABEL = "fs";
    static constexpr const char* LOCAL_FILE_SYSTEM_PARTITION_LABEL_ALT = "spiffs";

    // Debug
    static constexpr const char* MODULE_PREFIX = "FileSys";
};

extern FileSystem fileSystem;
