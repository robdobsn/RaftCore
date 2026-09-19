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
#include "NetworkSystem.h"
#include "esp_netif.h"
#include "esp_idf_version.h"

#define WARN_DNS_LOOKUP_FAILED
// #define DEBUG_DNS_LOOKUP
// #define DEBUG_DNS_LOOKUP_WHEN_NOT_CONNECTED

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
#endif
        return false;
    }

    // Lookup address (use base ESP_LOGX functions to avoid recursion in logging modules like LogPapertrail)
#ifdef DEBUG_DNS_LOOKUP
    ESP_LOGI(MODULE_PREFIX, "getIPAddr dns_gethostbyname %s", _hostname.c_str());
#endif

    // Start the lookup - dns_gethostbyname is part of the lwIP raw API so it must be executed in the context
    // of the lwIP tcpip thread. The state flags are also handled in that context so that there is no race with
    // the result callback (which also runs in that context and may occur before dns_gethostbyname returns)
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    // This call blocks until the function has executed on the tcpip thread
    if (esp_netif_tcpip_exec(lookupInTcpipContext, this) != ESP_OK)
        return false;
#else
    lookupInTcpipContext(this);
#endif

    // ERR_OK means the address was cached and is returned immediately
    if (_lookupStartErr == ERR_OK)
    {
#ifdef DEBUG_DNS_LOOKUP
        ESP_LOGI(MODULE_PREFIX, "getIPAddr lookup OK %s addr %s", _hostname.c_str(), ipaddr_ntoa(&_ipAddr));
#endif
        ipAddr = _ipAddr;
        return true;
    }

    // Check for ERR_INPROGRESS which means the lookup is in progress and the callback will carry the result
    if (_lookupStartErr == ERR_INPROGRESS)
        return false;

    // Any other error is a failure
#ifdef WARN_DNS_LOOKUP_FAILED
    ESP_LOGW(MODULE_PREFIX, "getIPAddr lookup FAILED %s error %d", _hostname.c_str(), _lookupStartErr);
#endif
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start lookup - executes in the context of the lwIP tcpip thread
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

esp_err_t DNSResolver::lookupInTcpipContext(void* pCtx)
{
    DNSResolver* pResolver = (DNSResolver*)pCtx;
    if (!pResolver)
        return ESP_ERR_INVALID_ARG;

    // Flag lookup in progress before starting (the result callback clears it)
    pResolver->_addrValid = false;
    pResolver->_lookupInProgress = true;

    // Set the IP address to blank and start the lookup
    IP_ADDR4(&pResolver->_ipAddr, 0,0,0,0);
    pResolver->_lookupStartErr = dns_gethostbyname(pResolver->_hostname.c_str(), &pResolver->_ipAddr, dnsResultCallback, pResolver);

    // If the lookup is in progress then the result callback handles the flags
    if (pResolver->_lookupStartErr == ERR_INPROGRESS)
        return ESP_OK;

    // Address was cached (ERR_OK) or lookup failed to start
    pResolver->_addrValid = (pResolver->_lookupStartErr == ERR_OK);
    pResolver->_lookupInProgress = false;
    return ESP_OK;
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

    // Check for error
    if (ipaddr == nullptr)
    {
        pResolver->_addrValid = false;
        pResolver->_lookupInProgress = false;
#ifdef WARN_DNS_LOOKUP_FAILED
        ESP_LOGW(MODULE_PREFIX, "dnsResultCallback lookup failed for %s\n", name);
#endif
        return;
    }

    // Set the IP address (data is written before the flags which are acted on by other tasks)
    pResolver->_ipAddr = *ipaddr;
    pResolver->_addrValid = true;
    pResolver->_lookupInProgress = false;
#ifdef DEBUG_DNS_LOOKUP
    ESP_LOGI(MODULE_PREFIX, "dnsResultCallback lookup OK for %s addr %s", name, ipaddr_ntoa(ipaddr));
#endif
}
