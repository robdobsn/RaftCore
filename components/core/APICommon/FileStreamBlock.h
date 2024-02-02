/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FileStreamBlock
// Collection of information about a block in a file or stream
//
// Rob Dobson 2018-2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <stddef.h>

class FileStreamBlock
{
public:
    FileStreamBlock(const FileStreamBlock& other)
    {
        filename = other.filename;
        contentLen = other.contentLen;
        filePos = other.filePos;
        pBlock = other.pBlock;
        blockLen = other.blockLen;
        finalBlock = other.finalBlock;
        crc16 = other.crc16;
        crc16Valid = other.crc16Valid;
        fileLen = other.fileLen;
        fileLenValid = other.fileLenValid;
        firstBlock = other.firstBlock;
    }
    FileStreamBlock(const char* filename,
        uint32_t contentLen,
        uint32_t filePos,
        const uint8_t* pBlock,
        uint32_t blockLen,
        bool finalBlock,
        uint32_t crc16,
        bool crc16Valid,
        uint32_t fileLen,
        bool fileLenValid,
        bool firstBlock
    )
    {
        this->filename = filename;
        this->contentLen = contentLen;
        this->filePos = filePos;
        this->pBlock = pBlock;
        this->blockLen = blockLen;
        this->finalBlock = finalBlock;
        this->crc16 = crc16;
        this->crc16Valid = crc16Valid;
        this->fileLen = fileLen;
        this->fileLenValid = fileLenValid;
        this->firstBlock = firstBlock;
    }
    FileStreamBlock(bool cancelUpdate)
    {
    }
    bool isCancelUpdate()
    {
        return (pBlock == nullptr) && (filename == nullptr);
    }
    const char* filename = nullptr;
    const uint8_t* pBlock = nullptr;
    uint32_t contentLen = 0;
    uint32_t filePos = 0;
    uint32_t blockLen = 0;
    uint32_t crc16 = 0;
    uint32_t fileLen = 0;
    bool finalBlock = false;
    bool crc16Valid = false;
    bool fileLenValid = false;
    bool firstBlock = false;
};
