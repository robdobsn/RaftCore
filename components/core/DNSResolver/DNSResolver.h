/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DNSResolver.h
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include "RaftArduino.h"
#include "lwip/dns.h"

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
    bool getIPAddr(ip_addr_t &ipAddr);

private:
    // State
    String _hostname;
    bool _addrValid = false;
    ip_addr_t _ipAddr;
    uint64_t _addrLastLookupMs = 0;
    static const uint32_t ADDR_REPEAT_FAILED_LOOKUP_MS = 5000;
    bool _lookupInProgress = false;

    // Helper
    static void dnsResultCallback(const char *name, const ip_addr_t *ipaddr, void *callback_arg);
};
