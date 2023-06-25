/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Raft Return Codes
//
// Rob Dobson 2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

enum RaftRetCode
{
    RAFT_RET_OK,
    RAFT_RET_BUSY,
    RAFT_RET_POS_MISMATCH,
    RAFT_RET_NOT_XFERING,
    RAFT_RET_NOT_STREAMING,
    RAFT_RET_SESSION_NOT_FOUND,
    RAFT_RET_CANNOT_START,
    RAFT_RET_INVALID_DATA,
    RAFT_RET_INVALID_OBJECT,
    RAFT_RET_INVALID_OPERATION,
    RAFT_RET_INSUFFICIENT_RESOURCE,
    RAFT_RET_OTHER_FAILURE,
    RAFT_RET_NOT_IMPLEMENTED,
};
