/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FileStreamBlockOwned
// Collection of information about a block in a file or stream - owns own copy of the block
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include "SpiramAwareAllocator.h"

class FileStreamBlockOwned
{
public:
    FileStreamBlockOwned()
    {
    }
    FileStreamBlockOwned(const char* filename,
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
        set(filename, contentLen, filePos, pBlock, blockLen, finalBlock, crc16, crc16Valid, 
            fileLen, fileLenValid, firstBlock);
    }
    void set(const char* filename,
        uint32_t contentLen,
        uint32_t filePos,
        const uint8_t* pBlock,
        uint32_t blockLen,
        bool finalBlock,
        uint32_t crc16,
        bool crc16Valid,
        uint32_t fileLen,
        bool fileLenValid,
        bool firstBlock)
    {
        if (filename)
            this->filename = filename;
        else
            this->filename = "";
        this->contentLen = contentLen;
        this->filePos = filePos;
        this->finalBlock = finalBlock;
        this->crc16 = crc16;
        this->crc16Valid = crc16Valid;
        this->fileLen = fileLen;
        this->fileLenValid = fileLenValid;
        this->firstBlock = firstBlock;
        if (pBlock and (blockLen > 0))
            block.assign(pBlock, pBlock+blockLen);
        else
            block.clear();
    }
    const uint8_t* getBlockData() const
    {
        return block.data();
    }
    uint32_t getBlockLen() const
    {
        return block.size();
    }
    uint32_t getFilePos() const
    {
        return filePos;
    }
    String filename;
    SpiramAwareUint8Vector block;
    uint32_t contentLen = 0;
    uint32_t filePos = 0;
    uint32_t crc16 = 0;
    uint32_t fileLen = 0;
    bool finalBlock = false;
    bool crc16Valid = false;
    bool fileLenValid = false;
    bool firstBlock = false;
};
