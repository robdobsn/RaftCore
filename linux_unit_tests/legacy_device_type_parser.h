/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// The device type record command-string parser as it was before Stage 4 of the device identification plan
// (copied from DeviceTypeRecords.cpp at RaftCore 3c8b284, with debug logging removed). Used only by
// test_device_type_grammar to check that DeviceTypeGrammar gives the same result for every string it accepts.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <vector>
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "RaftUtils.h"
#include "BusRequestInfo.h"
#include "PollReadLenExpr.h"
#include "DeviceTypeRecord.h"
#include "DevicePollingInfo.h"
#include "DeviceTypeRecords.h"

namespace LegacyParser
{
inline uint32_t extractBarAccessMs(const String& readStr);
inline bool extractBufferDataFromHexStr(const String& writeStr, std::vector<uint8_t>& writeData)
{        
    const char* pStr = writeStr.c_str();
    uint32_t inStrLen = writeStr.length();
    if (writeStr.startsWith("0x") || writeStr.startsWith("0X"))
    {
        pStr = writeStr.c_str() + 2;
        inStrLen -= 2;
    }
    // Compute length
    uint32_t writeStrLen = (inStrLen + 1) / 2;
    // Extract the write data
    writeData.resize(writeStrLen);
    Raft::getBytesFromHexStr(pStr, writeData.data(), writeData.size());
    return writeData.size() > 0 || (writeStr.length() == 0);
}

inline bool extractMaskAndDataFromHexStr(const String& readStr, std::vector<uint8_t>& readDataMask, 
            std::vector<uint8_t>& readDataCheck, bool maskToZeros, uint32_t& pauseAfterSendMs)
{
    readDataCheck.clear();
    readDataMask.clear();
    String readStrLC = readStr;
    readStrLC.toLowerCase();

    // Check bar access time
    pauseAfterSendMs = extractBarAccessMs(readStr);

    // If readStr contains rNNNN then it is a read request
    int readIdx = readStrLC.indexOf("r");
    if (readIdx >= 0)
    {
        // Compute length - a zero or missing length would be an empty check that matches anything
        uint32_t lenBytes = strtol(readStrLC.c_str() + readIdx + 1, NULL, 10);
        if (lenBytes == 0)
            return false;

        // Extract the read data
        readDataMask.resize(lenBytes);
        readDataCheck.resize(lenBytes);

        // Fill the same elements as before (indices readIdx to length-2) but only those within the
        // vectors - the loop runs over the string while the vectors are sized from the number in it,
        // so e.g. "r1p50" or "0x60=r1" previously wrote past the end of the heap allocation
        const int lastIdx = (int)readStrLC.length() - 2;
        for (int i = readIdx; (i <= lastIdx) && (i < (int)lenBytes); i++)
        {
            readDataMask[i] = maskToZeros ? 0xff : 0;
            readDataCheck[i] = 0;
        }
        return true;
    }

    // The kind of value (hex or binary) is decided by its prefix, after any leading whitespace and
    // optional leading pause (e.g. "p10 0x55"). Searching for "0x" anywhere is wrong because a binary
    // value contains "0x" wherever a 0 bit is followed by a don't-care bit (e.g. "0b1010xxxx")
    int valueIdx = 0;
    while ((valueIdx < (int)readStrLC.length()) && isspace(readStrLC[valueIdx]))
        valueIdx++;
    if ((valueIdx < (int)readStrLC.length()) && (readStrLC[valueIdx] == 'p'))
    {
        valueIdx++;
        while ((valueIdx < (int)readStrLC.length()) && isdigit(readStrLC[valueIdx]))
            valueIdx++;
        while ((valueIdx < (int)readStrLC.length()) && isspace(readStrLC[valueIdx]))
            valueIdx++;
    }
    const char* pValue = readStrLC.c_str() + valueIdx;

    // Check if the value starts with 0x
    if (strncmp(pValue, "0x", 2) == 0)
    {
        int hexIdx = valueIdx;
        // Compute length
        uint32_t lenBytes = (readStrLC.length() - hexIdx - 2 + 1) / 2;
        // Extract the read data
        readDataMask.resize(lenBytes);
        readDataCheck.resize(lenBytes);
        Raft::getBytesFromHexStr(readStrLC.c_str() + hexIdx + 2, readDataCheck.data(), readDataCheck.size());
        for (int i = 0; i < readDataMask.size(); i++)
        {
            readDataMask[i] = maskToZeros ? 0xff : 0;
        }
        return true;
    }

    // Check if the value starts with 0b
    else if (strncmp(pValue, "0b", 2) == 0)
    {
        int binIdx = valueIdx;
        // Compute length
        uint32_t lenBits = readStrLC.length() - binIdx - 2;
        uint32_t lenBytes = (lenBits + 7) / 8;
        // Extract the read data
        readDataMask.resize(lenBytes);
        readDataCheck.resize(lenBytes);
        uint32_t bitMask = 0x80;
        uint32_t byteIdx = 0;
        for (int i = binIdx + 2; i < readStrLC.length(); i++)
        {
            if (bitMask == 0x80)
            {
                readDataMask[byteIdx] = maskToZeros ? 0xff : 0;
                readDataCheck[byteIdx] = 0;
            }
            if (readStrLC[i] == 'x')
            {
                if (maskToZeros)
                    readDataMask[byteIdx] &= ~bitMask;
                else
                    readDataMask[byteIdx] |= bitMask;
            }
            else if (readStrLC[i] == '1')
            {
                readDataCheck[byteIdx] |= bitMask;
            }
            bitMask >>= 1;
            if (bitMask == 0)
            {
                bitMask = 0x80;
                byteIdx++;
            }
        }
        return true;
    }

    // Otherwise the only valid content is an optional pause (pNN), as in a write-then-pause step
    // such as "0x00FF=p25", which deliberately has nothing to read or check. Anything else (e.g.
    // "A005", a hex value missing its 0x prefix) is an error - accepting it as an empty check would
    // make detection match any device that ACKs the write
    for (uint32_t i = 0; i < readStrLC.length(); i++)
    {
        if (readStrLC[i] == 'p')
        {
            while ((i + 1 < readStrLC.length()) && isdigit(readStrLC[i + 1]))
                i++;
            continue;
        }
        if (!isspace(readStrLC[i]))
            return false;
    }
    return true;
}

inline bool extractCheckInfoFromHexStr(const String& readStr, std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>& checkValues,
            bool maskToZeros)
{
    checkValues.clear();
    // Iterate over comma separated sections of string
    String readStrLC = readStr;
    readStrLC.toLowerCase();
    while (readStrLC.length() > 0)
    {
        // Find next comma
        int sectionIdx = readStrLC.indexOf(",");
        if (sectionIdx < 0)
            sectionIdx = readStrLC.length();
        // Extract the check data
        std::vector<uint8_t> readDataMask;
        std::vector<uint8_t> readDataCheck;
        uint32_t pauseAfterSendMs = 0;
        if (!extractMaskAndDataFromHexStr(readStrLC.substring(0, sectionIdx), readDataMask, readDataCheck, maskToZeros, pauseAfterSendMs))
        {
            return false;
        }
        checkValues.push_back(std::make_pair(readDataMask, readDataCheck));
        readStrLC = readStrLC.substring(sectionIdx + 1);
    }
    return true;
}

inline uint32_t extractReadDataSize(const String& readStr)
{
    String readStrLC = readStr;
    readStrLC.toLowerCase();
    // Check for readStr starts with rNNNN (for read NNNN bytes)
    if (readStrLC.startsWith("r"))
    {
        return strtol(readStrLC.c_str() + 1, NULL, 10);
    }
    // Check if readStr starts with 0b 
    if (readStrLC.startsWith("0b"))
    {
        return (readStrLC.length() - 2 + 7) / 8;
    }
    return 0;
}

inline uint32_t extractBarAccessMs(const String& readStr)
{
    String readStrLC = readStr;
    readStrLC.toLowerCase();
    // Check for readStr having pNN in it
    int pauseIdx = readStrLC.indexOf("p");
    if (pauseIdx < 0)
    {
        return 0;
    }
    return strtol(readStrLC.c_str() + pauseIdx + 1, NULL, 10);
}

inline bool getDetectionRecs(const DeviceTypeRecord* pDevTypeRec, std::vector<DeviceTypeRecords::DeviceDetectionRec>& detectionRecs)
{
    // Clear initially
    detectionRecs.clear();
    if (!pDevTypeRec)
        return false;

    // Get the detection values
    String detectionValues = pDevTypeRec->detectionValues;

    // Extract name:value pairs
    std::vector<RaftJson::NameValuePair> detectionWriteReadPairs;
    RaftJson::extractNameValues(detectionValues, "=", "&", ";", detectionWriteReadPairs);

    // Convert to detection records
    // An invalid entry invalidates the whole record rather than being skipped - skipping it would
    // leave fewer checks (or none) and so make the record easier to match, not harder
    for (const auto& detectionNameValue : detectionWriteReadPairs)
    {
        DeviceTypeRecords::DeviceDetectionRec detectionRec;
        if (!extractBufferDataFromHexStr(detectionNameValue.name, detectionRec.writeData) ||
            !extractCheckInfoFromHexStr(detectionNameValue.value, detectionRec.checkValues, true))
        {
            detectionRecs.clear();
            return false;
        }
        detectionRec.pauseAfterSendMs = extractBarAccessMs(detectionNameValue.value);
        detectionRecs.push_back(detectionRec);
    }

    return true;
}

inline void getInitBusRequests(BusElemAddrType addr, const DeviceTypeRecord* pDevTypeRec, std::vector<BusRequestInfo>& initRequests)
{
    // Clear initially
    initRequests.clear();
    if (!pDevTypeRec)
        return;
        
    // Get the initialisation values
    String initValues = pDevTypeRec->initValues;

    // Extract name:value pairs
    std::vector<RaftJson::NameValuePair> initWriteReadPairs;
    RaftJson::extractNameValues(initValues, "=", "&", ";", initWriteReadPairs);

    // Form the bus requests
    for (const auto& initNameValue : initWriteReadPairs)
    {
        std::vector<uint8_t> writeData;
        if (!extractBufferDataFromHexStr(initNameValue.name, writeData))
            continue;
        uint32_t numReadDataBytes = extractReadDataSize(initNameValue.value);
        uint32_t barAccessForMs = extractBarAccessMs(initNameValue.value);

        // Create a bus request to write the initialisation value
        BusRequestInfo reqRec(BUS_REQ_TYPE_FAST_SCAN,
                    addr,
                    0, 
                    writeData.size(), 
                    writeData.data(),
                    numReadDataBytes,
                    barAccessForMs, 
                    nullptr, 
                    nullptr);
        initRequests.push_back(reqRec);

        // Debug
    }
}

inline void getPollReqs(BusElemAddrType addr, const String& pollRequest, std::vector<BusRequestInfo>& pollReqs,
            uint16_t& pollResultDataSize)
{
    pollReqs.clear();
    // Extract polling info
    std::vector<RaftJson::NameValuePair> pollWriteReadPairs;
    RaftJson::extractNameValues(pollRequest, "=", "&", ";", pollWriteReadPairs);

    // Create a polling request for each pair
    pollResultDataSize = 0;
    for (const auto& pollWriteReadPair : pollWriteReadPairs)
    {
        std::vector<uint8_t> writeData;
        if (!extractBufferDataFromHexStr(pollWriteReadPair.name, writeData))
        {
            continue;
        }

        // Check for dynamic read length expression r{...}
        String valueStr = pollWriteReadPair.value;
        int dynReadStart = valueStr.indexOf("r{");
        if (dynReadStart >= 0)
        {
            int dynReadEnd = valueStr.indexOf("}", dynReadStart + 2);
            if (dynReadEnd < 0)
            {
                continue;
            }

            // Extract the expression between r{ and }
            String exprStr = valueStr.substring(dynReadStart + 2, dynReadEnd);
            auto readLenExpr = std::make_shared<PollReadLenExpr>();
            if (!readLenExpr->parse(exprStr))
            {
                continue;
            }

            // Expression must have a :max value for buffer sizing
            if (!readLenExpr->hasMaxValue())
            {
                continue;
            }

            uint32_t maxReadLen = readLenExpr->getMaxValue();
            uint32_t pauseAfterSendMs = extractBarAccessMs(valueStr);

            // Create the poll request with the max read length
            BusRequestInfo pollReq(BUS_REQ_TYPE_POLL,
                    addr,
                    DevicePollingInfo::DEV_IDENT_POLL_CMD_ID,
                    writeData.size(),
                    writeData.data(),
                    maxReadLen,
                    pauseAfterSendMs,
                    NULL,
                    NULL);
            pollReq.setReadLenExpr(readLenExpr);
            pollReqs.push_back(pollReq);

            // Use max read length for buffer sizing
            pollResultDataSize += maxReadLen;

            continue;
        }

        std::vector<uint8_t> readDataMask;
        std::vector<uint8_t> readData;
        uint32_t pauseAfterSendMs = 0; 
        if (!extractMaskAndDataFromHexStr(pollWriteReadPair.value, readDataMask, readData, false, pauseAfterSendMs))
        {
            continue;
        }


        // Create the poll request
        BusRequestInfo pollReq(BUS_REQ_TYPE_POLL, 
                addr,
                DevicePollingInfo::DEV_IDENT_POLL_CMD_ID, 
                writeData.size(),
                writeData.data(), 
                readDataMask.size(),
                pauseAfterSendMs,
                NULL, 
                NULL);
        pollReqs.push_back(pollReq);

        // Keep track of poll result size
        pollResultDataSize += readData.size();
    }

}
}
