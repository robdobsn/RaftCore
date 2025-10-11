

#include "ProtocolExchange.h"

class MsgExchangeHookTest
{
public:
    MsgExchangeHookTest() :
        _protocolExchg(
            "MsgExchangeHookTest",
            _protocolExchgConfig)
    {
    }

    void loop()
    {
        
    }

private:
    RaftJson _protocolExchgConfig;
    ProtocolExchange _protocolExchg;
};

