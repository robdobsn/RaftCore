/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DNSResolver.cpp
// Resolves a hostname to an IP address
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftUtils.h"
#include "DNSResolver.h"

#define WARN_DNS_LOOKUP_FAILED
// #define DEBUG_DNS_LOOKUP
// #define DEBUG_DNS_LOOKUP_WHEN_NOT_CONNECTED

// Log prefix
static const char *MODULE_PREFIX = "DNSResolver";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get the IP address
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool DNSResolver::getIPAddr(ip_addr_t &ipAddr)
{
    // Check if lookup in progress
    if (_lookupInProgress)
        return false;

    // Check if address is valid
    if (_addrValid)
    {
        ipAddr = _ipAddr;
        return true;
    }

    // Check minimum time between lookups
    if (!Raft::isTimeout(millis(), _addrLastLookupMs, ADDR_REPEAT_FAILED_LOOKUP_MS))
        return false;

    // Set time of last lookup
    _addrLastLookupMs = millis();        

    // Check IP is connected
    if (!networkSystem.isIPConnected())
    {
#ifdef DEBUG_DNS_LOOKUP_WHEN_NOT_CONNECTED
        ESP_LOGI(MODULE_PREFIX, "getIPAddr not connected %s", _hostname.c_str());
        return false;
#endif
    }

    // Lookup address (use base ESP_LOGX functions to avoid recursion in logging modules like LogPapertrail)
#ifdef DEBUG_DNS_LOOKUP
    ESP_LOGI(MODULE_PREFIX, "getIPAddr dns_gethostbyname %s", _hostname.c_str());
#endif

    // Set the IP address to blank and start the lookup
    IP_ADDR4(&_ipAddr, 0,0,0,0);
    err_t dnsErr = dns_gethostbyname(_hostname.c_str(), &_ipAddr, dnsResultCallback, this);
    if (dnsErr == ERR_OK)
    {
        // ERR_OK means the address was cached and is returned immediately
#ifdef DEBUG_DNS_LOOKUP
        ESP_LOGI(MODULE_PREFIX, "getIPAddr lookup OK %s addr %s", _hostname.c_str(), ipaddr_ntoa(&_ipAddr));
#endif
        _addrValid = true;
        _lookupInProgress = false;
        ipAddr = _ipAddr;
        return true;
    }

    // Check for ERR_INPROGRESS which means the lookup is in progress and the callback will carry the result
    if (dnsErr == ERR_INPROGRESS)
    {
        _addrValid = false;
        _lookupInProgress = true;
        return false;
    }

    // Any other error is a failure
#ifdef WARN_DNS_LOOKUP_FAILED
    ESP_LOGW(MODULE_PREFIX, "getIPAddr lookup FAILED %s error %d", _hostname.c_str(), dnsErr);
#endif
    _addrValid = false;
    _lookupInProgress = false;
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DNS result callback
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DNSResolver::dnsResultCallback(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    // Lookup now complete
    DNSResolver* pResolver = (DNSResolver*)callback_arg;
    if (!pResolver)
        return;
    pResolver->_lookupInProgress = false;

    // Check for error
    if (ipaddr == nullptr)
    {
        pResolver->_addrValid = false;
#ifdef WARN_DNS_LOOKUP_FAILED
        ESP_LOGW(MODULE_PREFIX, "dnsResultCallback lookup failed for %s\n", name);
#endif
        return;
    }

    // Set the IP address
    pResolver->_ipAddr = *ipaddr;
    pResolver->_addrValid = true;
#ifdef DEBUG_DNS_LOOKUP
    ESP_LOGI(MODULE_PREFIX, "dnsResultCallback lookup OK for %s addr %s", name, ipaddr_ntoa(ipaddr));
#endif
}
