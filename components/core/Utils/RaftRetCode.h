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
    RAFT_OK,
    RAFT_BUSY,
    RAFT_POS_MISMATCH,
    RAFT_NOT_XFERING,
    RAFT_NOT_STREAMING,
    RAFT_SESSION_NOT_FOUND,
    RAFT_CANNOT_START,
    RAFT_INVALID_DATA,
    RAFT_INVALID_OBJECT,
    RAFT_INVALID_OPERATION,
    RAFT_INSUFFICIENT_RESOURCE,
    RAFT_OTHER_FAILURE,
    RAFT_NOT_IMPLEMENTED,
};
