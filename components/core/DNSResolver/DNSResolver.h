/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DNSResolver.h
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <atomic>
#include "RaftArduino.h"
#include "lwip/dns.h"
#include "esp_err.h"

class DNSResolver
{
public:
    // Set the hostname to resolve
    void setHostname(const char *hostname)
    {
        // Set the hostname
        _hostname = hostname;
        _lookupInProgress = false;
        _addrValid = false;
    }

    // Get hostname
    const char *getHostname() 
    { 
        return _hostname.c_str(); 
    }

    // Get IP address
    // Note: this must NOT be called from the lwIP tcpip thread (e.g. from an lwIP callback) since the
    // lookup is executed in the context of the tcpip thread and this function waits for that to happen
    bool getIPAddr(ip_addr_t &ipAddr);

private:
    // State
    // _addrValid and _lookupInProgress are written in the context of the lwIP tcpip thread
    // and _ipAddr is always written before _addrValid is set
    String _hostname;
    std::atomic<bool> _addrValid{false};
    ip_addr_t _ipAddr;
    uint64_t _addrLastLookupMs = 0;
    static const uint32_t ADDR_REPEAT_FAILED_LOOKUP_MS = 5000;
    std::atomic<bool> _lookupInProgress{false};

    // Result of starting lookup (in tcpip thread context)
    err_t _lookupStartErr = ERR_OK;

    // Helpers
    static esp_err_t lookupInTcpipContext(void* pCtx);
    static void dnsResultCallback(const char *name, const ip_addr_t *ipaddr, void *callback_arg);

    // Debug
    static constexpr const char* MODULE_PREFIX = "DNSResolver";
};
