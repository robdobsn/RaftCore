/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FileSystemChunker
//
// Rob Dobson 2018-2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "FileSystemChunker.h"
#include "FileSystem.h"

// Warn
// #define WARN_ON_FILE_CHUNKER_START_FAIL

// Debug
// #define DEBUG_FILE_CHUNKER_START_END
// #define DEBUG_FILE_CHUNKER_CHUNKS
// #define DEBUG_FILE_CHUNKER_PERFORMANCE
// #define DEBUG_FILE_CHUNKER_CONTENTS
#define DEBUG_FILE_CHUNKER_READ_THRESH_MS 100

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileSystemChunker::FileSystemChunker()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileSystemChunker::~FileSystemChunker()
{
    if (_pFile)
        fileSystem.fileClose(_pFile, "", _filePath, _writing);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start chunk access
// chunkMaxLen may be 0 if writing
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystemChunker::start(const String& filePath, uint32_t chunkMaxLen, bool readByLine, 
            bool writing, bool keepOpen, bool keepOpenEvenIfAtEnd)
{
    // Check if already busy
    if (_isActive)
        return false;

    // Check file details
    if (!writing && !fileSystem.getFileInfo("", filePath, _fileLen))
    {
#ifdef WARN_ON_FILE_CHUNKER_START_FAIL
        LOG_W(MODULE_PREFIX, "start cannot getFileInfo %s", filePath.c_str());
#endif
        return false;
    }

    // Store params
    _chunkMaxLen = chunkMaxLen;
    _readByLine = readByLine;
    _filePath = filePath;
    _writing = writing;
    _keepOpen = keepOpen;
    _keepOpenEvenIfAtEnd = keepOpenEvenIfAtEnd;

    // Now active
    _isActive = true;
    _curPos = 0;

#ifdef DEBUG_FILE_CHUNKER_START_END
    // Debug
    LOG_I(MODULE_PREFIX, "start filename %s size %d byLine %s", 
            filePath.c_str(), _fileLen, (_readByLine ? "Y" : "N"));
#endif
    return true; 
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Next chunk read
// Returns false on failure
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystemChunker::nextRead(uint8_t* pBuf, uint32_t bufLen, uint32_t& handledBytes, bool& finalChunk)
{
    // Check valid
    finalChunk = false;
    if (!_isActive || !pBuf || _writing)
        return false;

    // Ensure we don't read beyond buffer
    uint32_t maxToRead = (bufLen < _chunkMaxLen) || (_chunkMaxLen == 0) ? bufLen : _chunkMaxLen;
    handledBytes = 0;

    // Check if keep open
    if (_keepOpen)
        return nextReadKeepOpen(pBuf, bufLen, handledBytes, finalChunk, maxToRead);

    // Check if read by line
    if (_readByLine)
    {
        uint32_t finalFilePos = 0;
        bool readOk = fileSystem.getFileLine("", _filePath, _curPos, pBuf, maxToRead, finalFilePos);
        _curPos = finalFilePos;
        if (!readOk)
        {
            _isActive = false;
            finalChunk = true;
        }
        else
        {
            handledBytes = strnlen((char*)pBuf, bufLen);
        }

#ifdef DEBUG_FILE_CHUNKER_CHUNKS
        // Debug
        LOG_I(MODULE_PREFIX, "next byLine filename %s readOk %s pos %d read %d busy %s", 
                _filePath.c_str(), readOk ? "YES" : "NO", _curPos, handledBytes, _isActive ? "YES" : "NO");
#endif

        return readOk;
    }

    // Must be reading blocks
    bool readOk = fileSystem.getFileSection("", _filePath, _curPos, pBuf, maxToRead, handledBytes);
    _curPos += handledBytes;
    if (!readOk)
    {
        _isActive = false;
    }
    if (_curPos == _fileLen)
    {
        _isActive = false;
        finalChunk = true; 
    }

#ifdef DEBUG_FILE_CHUNKER_CHUNKS
        // Debug
        LOG_I(MODULE_PREFIX, "next binary filename %s readOk %s pos %d read %d busy %s", 
                _filePath.c_str(), readOk ? "YES" : "NO", _curPos, handledBytes, _isActive ? "YES" : "NO");
#endif
#ifdef DEBUG_FILE_CHUNKER_CONTENTS
        String debugStr;
        Raft::getHexStrFromBytes(pBuf, bufLen, debugStr);
        LOG_I(MODULE_PREFIX, "CHUNK: %s", debugStr.c_str());
#endif

    return readOk;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Next chunk read
// Returns next chunk
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SpiramAwareUint8Vector FileSystemChunker::nextRead(uint32_t maxLen, bool& finalChunk)
{
    // Check valid
    finalChunk = false;
    if (!_isActive || _writing)
        return SpiramAwareUint8Vector();

    // Ensure we don't read beyond buffer
    uint32_t maxToRead = (maxLen < _chunkMaxLen) || (_chunkMaxLen == 0) ? maxLen : _chunkMaxLen;

    // Check if keep open
    if (_keepOpen)
        return nextReadKeepOpen(maxToRead, finalChunk, maxToRead);

    // Check if read by line
    if (_readByLine)
    {
        uint32_t finalFilePos = 0;
        String line = fileSystem.getFileLine("", _filePath, _curPos, maxToRead, finalFilePos);
        _curPos = finalFilePos;
        if (line.length() == 0)
        {
            _isActive = false;
            finalChunk = true;
        }

#ifdef DEBUG_FILE_CHUNKER_CHUNKS
        // Debug
        LOG_I(MODULE_PREFIX, "next byLine filename %s pos %d read %d busy %s", 
                _filePath.c_str(), _curPos, line.length(), _isActive ? "YES" : "NO");
#endif

        return SpiramAwareUint8Vector(line.c_str(), line.c_str() + line.length());
    }

    // Must be reading blocks
    auto chunk = fileSystem.getFileSection("", _filePath, _curPos, maxToRead);
    _curPos += chunk.size();
    if (chunk.size() == 0)
    {
        _isActive = false;
    }
    if (_curPos == _fileLen)
    {
        _isActive = false;
        finalChunk = true; 
    }

#ifdef DEBUG_FILE_CHUNKER_CHUNKS
        // Debug
        LOG_I(MODULE_PREFIX, "next binary filename %s pos %d read %d busy %s", 
                _filePath.c_str(), _curPos, chunk.size(), _isActive ? "YES" : "NO");
#endif
#ifdef DEBUG_FILE_CHUNKER_CONTENTS
        String debugStr;
        Raft::getHexStr(chunk, debugStr);
        LOG_I(MODULE_PREFIX, "CHUNK: %s", debugStr.c_str());
#endif

    return chunk;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Next chunk read
// Returns false on failure
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystemChunker::nextReadKeepOpen(uint8_t* pBuf, uint32_t bufLen, uint32_t& handledBytes, 
                        bool& finalChunk, uint32_t numToRead)
{
#ifdef DEBUG_FILE_CHUNKER_READ_THRESH_MS
    uint32_t debugChunkerReadStartMs = millis();
    uint32_t debugStartMs = millis();
    uint32_t debugFileOpenTimeMs = 0;
    uint32_t debugReadTimeMs = 0;
    uint32_t debugCloseTimeMs = 0;
#endif
#ifdef DEBUG_FILE_CHUNKER_CHUNKS
    uint32_t debugBeforeFilePos = 0;
    uint32_t debugAfterFilePos = 0;
#endif

    // Check if file is open already
    if (!_pFile)
    {
        _pFile = fileSystem.fileOpen("", _filePath, _writing, _curPos);
#ifdef DEBUG_FILE_CHUNKER_READ_THRESH_MS
        debugFileOpenTimeMs = millis() - debugStartMs;
        debugStartMs = millis();
#endif
    }

    // Read if valid
    if (_pFile && pBuf)
    {
#ifdef DEBUG_FILE_CHUNKER_CHUNKS
        // File pos
        debugBeforeFilePos = fileSystem.filePos(_pFile);
#endif
        
        // Check num to read valid
        if (numToRead > bufLen)
            numToRead = bufLen;

        // Read
        handledBytes = fileSystem.fileRead(_pFile, pBuf, numToRead);

#ifdef DEBUG_FILE_CHUNKER_READ_THRESH_MS
        debugReadTimeMs = millis() - debugStartMs;
        debugStartMs = millis();
#endif

#ifdef DEBUG_FILE_CHUNKER_CHUNKS
        // File pos
        debugAfterFilePos = fileSystem.filePos(_pFile);
#endif

        // Check for close
        if (handledBytes != numToRead)
        {
            // Close on final chunk
            finalChunk = true;

            // Check for close at end
            if (!_keepOpenEvenIfAtEnd)
            {
                fileSystem.fileClose(_pFile, "", _filePath, _writing);
                _pFile = nullptr;
                _isActive = false;
            }
        }
    }

#ifdef DEBUG_FILE_CHUNKER_READ_THRESH_MS
    if (millis() - debugChunkerReadStartMs > DEBUG_FILE_CHUNKER_READ_THRESH_MS)
    {
        debugCloseTimeMs = millis() - debugStartMs;
        LOG_I(MODULE_PREFIX, "nextReadKeepOpen fileOpen %dms read %dms close %dms filename %s readBytes %d busy %s", 
                debugFileOpenTimeMs, debugReadTimeMs, debugCloseTimeMs,
                _filePath.c_str(), handledBytes, _isActive ? "YES" : "NO");
    }
#endif
#ifdef DEBUG_FILE_CHUNKER_CHUNKS
    // Debug
    LOG_I(MODULE_PREFIX, "nextReadKeepOpen filename %s filePos: before %d after %d readBytes %d busy %s", 
            _filePath.c_str(), debugBeforeFilePos, debugAfterFilePos, handledBytes, _isActive ? "YES" : "NO");
#endif
#ifdef DEBUG_FILE_CHUNKER_CONTENTS
    String debugStr;
    Raft::getHexStrFromBytes(pBuf, handledBytes, debugStr);
    LOG_I(MODULE_PREFIX, "nextReadKeepOpen: %s", debugStr.c_str());
#endif

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Next chunk read and keep file open
// Returns chunk read
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SpiramAwareUint8Vector FileSystemChunker::nextReadKeepOpen(uint32_t maxLen, 
                        bool& finalChunk, uint32_t numToRead)
{
#ifdef DEBUG_FILE_CHUNKER_READ_THRESH_MS
    uint32_t debugChunkerReadStartMs = millis();
    uint32_t debugStartMs = millis();
    uint32_t debugFileOpenTimeMs = 0;
    uint32_t debugReadTimeMs = 0;
    uint32_t debugCloseTimeMs = 0;
#endif
#ifdef DEBUG_FILE_CHUNKER_CHUNKS
    uint32_t debugBeforeFilePos = 0;
    uint32_t debugAfterFilePos = 0;
#endif

    // Check if file is open already
    SpiramAwareUint8Vector readData;
    if (!_pFile)
    {
        _pFile = fileSystem.fileOpen("", _filePath, _writing, _curPos);
#ifdef DEBUG_FILE_CHUNKER_READ_THRESH_MS
        debugFileOpenTimeMs = millis() - debugStartMs;
        debugStartMs = millis();
#endif
    }

    // Read if valid
    if (_pFile)
    {
#ifdef DEBUG_FILE_CHUNKER_CHUNKS
        // File pos
        debugBeforeFilePos = fileSystem.filePos(_pFile);
#endif
        
        // Check num to read valid
        if (numToRead > maxLen)
            numToRead = maxLen;

        // Read
        readData = fileSystem.fileRead(_pFile, numToRead);

#ifdef DEBUG_FILE_CHUNKER_READ_THRESH_MS
        debugReadTimeMs = millis() - debugStartMs;
        debugStartMs = millis();
#endif

#ifdef DEBUG_FILE_CHUNKER_CHUNKS
        // File pos
        debugAfterFilePos = fileSystem.filePos(_pFile);
#endif

        // Check for close
        if (readData.size() != numToRead)
        {
            // Close on final chunk
            finalChunk = true;

            // Check for close at end
            if (!_keepOpenEvenIfAtEnd)
            {
                fileSystem.fileClose(_pFile, "", _filePath, _writing);
                _pFile = nullptr;
                _isActive = false;
            }
        }
    }

#ifdef DEBUG_FILE_CHUNKER_READ_THRESH_MS
    if (millis() - debugChunkerReadStartMs > DEBUG_FILE_CHUNKER_READ_THRESH_MS)
    {
        debugCloseTimeMs = millis() - debugStartMs;
        LOG_I(MODULE_PREFIX, "nextReadKeepOpen fileOpen %dms read %dms close %dms filename %s readBytes %d busy %s", 
                debugFileOpenTimeMs, debugReadTimeMs, debugCloseTimeMs,
                _filePath.c_str(), (int)readData.size(), _isActive ? "YES" : "NO");
    }
#endif
#ifdef DEBUG_FILE_CHUNKER_CHUNKS
    // Debug
    LOG_I(MODULE_PREFIX, "nextReadKeepOpen filename %s filePos: before %d after %d readBytes %d busy %s", 
            _filePath.c_str(), debugBeforeFilePos, debugAfterFilePos, readData.size(), _isActive ? "YES" : "NO");
#endif
#ifdef DEBUG_FILE_CHUNKER_CONTENTS
    String debugStr;
    Raft::getHexStr(readData, debugStr);
    LOG_I(MODULE_PREFIX, "nextReadKeepOpen: %s", debugStr.c_str());
#endif

    return readData;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Next chunk write
// Returns false on failure
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystemChunker::nextWrite(const uint8_t* pBuf, uint32_t bufLen, uint32_t& handledBytes, bool& finalChunk)
{
#ifdef DEBUG_FILE_CHUNKER_PERFORMANCE
    uint32_t startMs = millis();
    uint32_t fileOpenMs = 0;
#endif

    // Check valid
    handledBytes = 0;
    if (!_isActive)
        return false;

    // Check if file is open already
    if (!_pFile)
    {
        _pFile = fileSystem.fileOpen("", _filePath, _writing, _curPos);
#ifdef DEBUG_FILE_CHUNKER_PERFORMANCE
        fileOpenMs = millis();
#endif
    }

    // Write if valid
    bool writeOk = false;
    if (_pFile)
    {
        // Write
        if (pBuf && bufLen > 0)
            handledBytes = fileSystem.fileWrite(_pFile, pBuf, bufLen);
        writeOk = handledBytes == bufLen;

        // Check if we should keep open
        if (!_keepOpen || finalChunk)
        {
            fileSystem.fileClose(_pFile, "", _filePath, _writing);
            _pFile = nullptr;
        }
    }

#ifdef DEBUG_FILE_CHUNKER_CHUNKS
        // Debug
        LOG_I(MODULE_PREFIX, "nextWrite filename %s writeOk %s written %d busy %s", 
                _filePath.c_str(), writeOk ? "YES" : "NO", handledBytes, _isActive ? "YES" : "NO");
#endif
#ifdef DEBUG_FILE_CHUNKER_CONTENTS
        String debugStr;
        Raft::getHexStrFromBytes(pBuf, bufLen, debugStr);
        LOG_I(MODULE_PREFIX, "nextWrite: %s", debugStr.c_str());
#endif

#ifdef DEBUG_FILE_CHUNKER_PERFORMANCE
    LOG_I(MODULE_PREFIX, "nextWrite elapsed ms %d file open %d", 
                (int)(millis() - startMs), fileOpenMs != 0 ? fileOpenMs - startMs : 0);
#endif

    return writeOk;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// End
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileSystemChunker::end()
{
    _isActive = false;
    relax();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Relax (closes file if open)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileSystemChunker::relax()
{
    // Check if open
    if (_pFile)
    {
        fileSystem.fileClose(_pFile, "", _filePath, _writing);
        _pFile = nullptr;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Reset (return to start)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileSystemChunker::restart()
{
    // Rewind file if file already open
    if (_pFile)
        fileSystem.fileSeek(_pFile, 0);

    // Active and at start
    _isActive = true;
    _curPos = 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Seek
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystemChunker::seek(uint32_t pos)
{
    // Check if open
    if (_isActive && _pFile)
    {
        // Seek
        if (fileSystem.fileSeek(_pFile, pos))
        {
            _curPos = pos;
            return true;
        }
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get file position
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t FileSystemChunker::getFilePos() const
{
    if (_keepOpen)
    {
        if (_pFile)
            return fileSystem.filePos(_pFile);
        return 0;
    }
    return _curPos;
}
