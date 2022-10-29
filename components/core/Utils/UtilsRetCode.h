/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Utils Return Codes
//
// Rob Dobson 2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

class UtilsRetCode
{
public:
    enum RetCode
    {
        OK,
        BUSY,
        POS_MISMATCH,
        NOT_UPLOADING,
        NOT_STREAMING,
        SESSION_NOT_FOUND,
        CANNOT_START,
        INVALID_DATA,
        INVALID_OBJECT,
        INVALID_OPERATION,
        INSUFFICIENT_RESOURCE,
        OTHER_FAILURE,
    };

    static const char* getRetcStr(RetCode retc)
    {
        switch(retc)
        {
            case OK: return "OK";
            case BUSY: return "BUSY";
            case POS_MISMATCH: return "POS_MISMATCH";
            case NOT_UPLOADING: return "NOT_UPLOADING";
            case NOT_STREAMING: return "NOT_STREAMING";
            case SESSION_NOT_FOUND: return "SESSION_NOT_FOUND";
            case CANNOT_START: return "CANNOT_START";
            case INVALID_DATA: return "INVALID_DATA";
            case INVALID_OBJECT: return "INVALID_OBJECT";
            case INVALID_OPERATION: return "INVALID_OPERATION";
            case INSUFFICIENT_RESOURCE: return "INSUFFICIENT_RESOURCE";
            case OTHER_FAILURE: return "OTHER_FAILURE";
            default: return "UNKNOWN";
        }
    };
};
