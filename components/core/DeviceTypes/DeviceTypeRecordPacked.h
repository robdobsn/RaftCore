/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceTypeRecordPacked.h
//
// Storage for one extended device type record in a single allocation (in PSRAM when the board has it):
//
//   [DeviceTypeRecord view][key\0][addresses\0][detectionValues\0][initValues\0][pollInfo\0][devInfoJson\0]
//   [hwTypeId\0 (if it has one)][generalCall\0 (if it has one)]
//
// The view's string pointers point into the same allocation, so a lookup returns a const DeviceTypeRecord*
// for an extended record just as for a base record (which is in flash). A packed record is never changed or
// moved after it is created, so pointers to it stay valid for as long as it exists.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <string.h>
#include <new>
#include "DeviceTypeRecord.h"
#include "DeviceTypeRecordDynamic.h"
#include "SpiramAwareAllocator.h"

class DeviceTypeRecordPacked
{
public:
    /// @brief Bytes a packed copy of a record takes
    /// @param src record
    /// @return size of the allocation in bytes
    static uint32_t sizeFor(const DeviceTypeRecordDynamic& src)
    {
        return sizeof(DeviceTypeRecord) +
                src.deviceTypeName.length() + 1 +
                src.addresses.length() + 1 +
                src.detectionValues.length() + 1 +
                src.initValues.length() + 1 +
                src.pollInfo.length() + 1 +
                src.devInfoJson.length() + 1 +
                (src.hwTypeId.length() > 0 ? src.hwTypeId.length() + 1 : 0) +
                (src.generalCall.length() > 0 ? src.generalCall.length() + 1 : 0);
    }

    /// @brief Create a packed copy of a record
    /// @param src record
    /// @return the record view (at the start of the allocation), or nullptr if out of memory
    static const DeviceTypeRecord* create(const DeviceTypeRecordDynamic& src)
    {
        uint32_t size = sizeFor(src);
        uint8_t* pMem = SpiramAwareAllocator<uint8_t>().allocate(size);
        if (!pMem)
            return nullptr;
        DeviceTypeRecord* pRec = new (pMem) DeviceTypeRecord();
        char* pStr = (char*)(pMem + sizeof(DeviceTypeRecord));
        pRec->deviceType = copyStr(pStr, src.deviceTypeName.c_str(), src.deviceTypeName.length());
        pRec->addresses = copyStr(pStr, src.addresses.c_str(), src.addresses.length());
        pRec->detectionValues = copyStr(pStr, src.detectionValues.c_str(), src.detectionValues.length());
        pRec->initValues = copyStr(pStr, src.initValues.c_str(), src.initValues.length());
        pRec->pollInfo = copyStr(pStr, src.pollInfo.c_str(), src.pollInfo.length());
        pRec->devInfoJson = copyStr(pStr, src.devInfoJson.c_str(), src.devInfoJson.length());
        pRec->pollDataSizeBytes = src.pollDataSizeBytes;
        pRec->pollResultDecodeFn = src.pollResultDecodeFn;
        pRec->scanPriority = src.scanPriority;
        pRec->flags = src.flags;
        pRec->identPriority = src.identPriority;
        pRec->hwTypeId = src.hwTypeId.length() > 0 ? copyStr(pStr, src.hwTypeId.c_str(), src.hwTypeId.length()) : nullptr;
        pRec->initialPolicy = src.initialPolicy;
        pRec->generalCall = src.generalCall.length() > 0 ? copyStr(pStr, src.generalCall.c_str(), src.generalCall.length()) : nullptr;
        return pRec;
    }

    /// @brief Free a packed record
    /// @param pRec record view returned by create()
    static void destroy(const DeviceTypeRecord* pRec)
    {
        if (pRec)
            SpiramAwareAllocator<uint8_t>().deallocate((void*)pRec, 0);
    }

    /// @brief Check whether a record has the same content as a packed record
    /// @param packed packed record
    /// @param src record
    /// @return true if every field matches
    static bool contentMatches(const DeviceTypeRecord& packed, const DeviceTypeRecordDynamic& src)
    {
        return strEq(packed.deviceType, src.deviceTypeName.c_str()) &&
                strEq(packed.addresses, src.addresses.c_str()) &&
                strEq(packed.detectionValues, src.detectionValues.c_str()) &&
                strEq(packed.initValues, src.initValues.c_str()) &&
                strEq(packed.pollInfo, src.pollInfo.c_str()) &&
                strEq(packed.devInfoJson, src.devInfoJson.c_str()) &&
                (packed.pollDataSizeBytes == src.pollDataSizeBytes) &&
                // A record given no decode function may have one attached (inherited from the base record, or
                // made at runtime) - that is still the same record
                (!src.pollResultDecodeFn || (packed.pollResultDecodeFn == src.pollResultDecodeFn)) &&
                (packed.scanPriority == src.scanPriority) && (packed.flags == src.flags) &&
                (packed.identPriority == src.identPriority) && strEq(packed.hwTypeId, src.hwTypeId.c_str()) &&
                (packed.initialPolicy == src.initialPolicy) && strEq(packed.generalCall, src.generalCall.c_str());
    }

private:
    // Copy a string to pDest (NUL terminated), advance pDest past it and return where it was copied to
    static const char* copyStr(char*& pDest, const char* pSrc, uint32_t len)
    {
        char* pStart = pDest;
        if (len > 0)
            memcpy(pDest, pSrc, len);
        pDest[len] = 0;
        pDest += len + 1;
        return pStart;
    }

    static bool strEq(const char* pA, const char* pB)
    {
        return strcmp(pA ? pA : "", pB ? pB : "") == 0;
    }
};
