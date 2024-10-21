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
    RAFT_BUS_PENDING,
    RAFT_BUS_HW_TIME_OUT,
    RAFT_BUS_ACK_ERROR,
    RAFT_BUS_ARB_LOST,
    RAFT_BUS_SW_TIME_OUT,
    RAFT_BUS_INVALID,
    RAFT_BUS_NOT_READY,
    RAFT_BUS_INCOMPLETE,
    RAFT_BUS_BARRED,
    RAFT_BUS_NOT_INIT,
    RAFT_BUS_STUCK,
    RAFT_BUS_SLOT_POWER_UNSTABLE
};
