/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// WiFiDiagnostics
// WiFi station link diagnostics and runtime link options
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "WiFiDiagnostics.h"
#include "Logger.h"
#include "RaftUtils.h"
#include "ping/ping_sock.h"
#include "lwip/inet.h"
#include "esp_heap_caps.h"

static const char* MODULE_PREFIX = "WiFiDiag";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
WiFiDiagnostics::WiFiDiagnostics()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
WiFiDiagnostics::~WiFiDiagnostics()
{
    stopPing();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle a WiFi event (called from the event loop task)
/// @param eventId WiFi event ID
/// @param pEventData event data
void WiFiDiagnostics::onWiFiEvent(int32_t eventId, void* pEventData)
{
    switch (eventId)
    {
    case WIFI_EVENT_STA_CONNECTED:
        _connects++;
        _connectedAtMs = millis();
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        _disconnects++;
        if (pEventData)
            _lastDisconnectReason = ((const wifi_event_sta_disconnected_t*)pEventData)->reason;
        break;
    case WIFI_EVENT_STA_BEACON_TIMEOUT:
        _beaconTimeouts++;
        break;
    case WIFI_EVENT_STA_BSS_RSSI_LOW:
        _rssiLowEvents++;
        break;
    default:
        break;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle IP address obtained (called from the event loop task)
void WiFiDiagnostics::onGotIP()
{
    _gotIPs++;
    applyConnectionOptions();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle IP address lost (called from the event loop task)
void WiFiDiagnostics::onLostIP()
{
    _lostIPs++;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Apply options which must be (re)applied after each connection
void WiFiDiagnostics::applyConnectionOptions()
{
    esp_wifi_set_ps(_psMode);
    if (_txPwrDbm > 0)
        esp_wifi_set_max_tx_power((int8_t)(_txPwrDbm * 4));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service (called from the main task)
/// @param pStaNetIf station network interface
/// @param staConnectedWithIP true if the station is connected and has an IP address
void WiFiDiagnostics::loop(esp_netif_t* pStaNetIf, bool staConnectedWithIP)
{
    // Start/stop/restart the gateway ping as required
    bool pingWanted = staConnectedWithIP && (_pingIntervalMs > 0) && pStaNetIf;
    if (_pingActive && (!pingWanted || (_pingActiveIntervalMs != _pingIntervalMs)))
        stopPing();
    if (pingWanted && !_pingActive && Raft::isTimeout(millis(), _pingStartAttemptMs, PING_START_RETRY_MS))
    {
        _pingStartAttemptMs = millis();
        startPing(pStaNetIf);
    }

    // Recover if the gateway can't be reached although the station is connected
    if (_pingActive && (_pingRecoverCount > 0) && (_pingConsecFail >= _pingRecoverCount))
    {
        _pingRecoveries++;
        LOG_W(MODULE_PREFIX, "gateway %s unreachable for %d pings - reconnecting (recovery %d)",
                    _pingTarget.c_str(), (int)_pingConsecFail.load(), (int)_pingRecoveries.load());
        logSummary();
        _pingConsecFail = 0;
        stopPing();
        forceReconnect();
    }

    // Periodic summary
    if ((_logIntervalS > 0) && Raft::isTimeout(millis(), _logLastMs, _logIntervalS * 1000))
    {
        _logLastMs = millis();
        logSummary();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set options from JSON
/// @param options options (see header)
/// @param errStr (out) error description
/// @return true if all options were valid
bool WiFiDiagnostics::setOptions(const RaftJsonIF& options, String& errStr)
{
    // Power save
    String ps = options.getString("ps", "");
    if (ps.length() > 0)
    {
        if (ps.equalsIgnoreCase("none")) _psMode = WIFI_PS_NONE;
        else if (ps.equalsIgnoreCase("min")) _psMode = WIFI_PS_MIN_MODEM;
        else if (ps.equalsIgnoreCase("max")) _psMode = WIFI_PS_MAX_MODEM;
        else { errStr = "invalidPS"; return false; }
        esp_wifi_set_ps(_psMode);
    }

    // TX power
    if (options.contains("txPwr"))
    {
        int txPwr = options.getLong("txPwr", 0);
        if ((txPwr != 0) && ((txPwr < 2) || (txPwr > 20)))
        {
            errStr = "invalidTxPwr";
            return false;
        }
        _txPwrDbm = txPwr;
        // 0 restores the maximum (20dBm = 80 quarter dBm)
        esp_wifi_set_max_tx_power((int8_t)((txPwr > 0 ? txPwr : 20) * 4));
    }

    // Protocol and bandwidth (these take effect on the next connection so reconnect if changed)
    bool reconnectRequired = false;
    String proto = options.getString("proto", "");
    if (proto.length() > 0)
    {
        uint8_t bitmap = 0;
        if (proto.equalsIgnoreCase("bgn")) bitmap = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N;
        else if (proto.equalsIgnoreCase("bg")) bitmap = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G;
        else if (proto.equalsIgnoreCase("b")) bitmap = WIFI_PROTOCOL_11B;
        else { errStr = "invalidProto"; return false; }
        _protocolBitmap = bitmap;
        esp_err_t err = esp_wifi_set_protocol(WIFI_IF_STA, _protocolBitmap);
        if (err != ESP_OK) { errStr = "setProtoFailed"; return false; }
        _protocolOrBwSet = true;
        reconnectRequired = true;
    }
    if (options.contains("bw"))
    {
        int bw = options.getLong("bw", 20);
        _bandwidth = (bw == 40) ? WIFI_BW40 : WIFI_BW20;
        esp_err_t err = esp_wifi_set_bandwidth(WIFI_IF_STA, _bandwidth);
        if (err != ESP_OK) { errStr = "setBwFailed"; return false; }
        _protocolOrBwSet = true;
        reconnectRequired = true;
    }

    // Gateway ping and recovery
    if (options.contains("pingMs"))
        _pingIntervalMs = options.getLong("pingMs", 0);
    if (options.contains("pingRecover"))
        _pingRecoverCount = options.getLong("pingRecover", 0);

    // Summary log
    if (options.contains("logS"))
        _logIntervalS = options.getLong("logS", 0);

    // Reconnect
    if (reconnectRequired || options.getBool("reconnect", false))
        forceReconnect();
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get diagnostics as JSON
/// @param includeBraces true to include outer braces
/// @return JSON string
String WiFiDiagnostics::getJSON(bool includeBraces) const
{
    // Link info
    wifi_ap_record_t ap = {};
    bool apValid = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    wifi_ps_type_t ps = WIFI_PS_NONE;
    esp_wifi_get_ps(&ps);
    int8_t txPwrQdBm = 0;
    esp_wifi_get_max_tx_power(&txPwrQdBm);
    uint8_t proto = 0;
    esp_wifi_get_protocol(WIFI_IF_STA, &proto);
    wifi_bandwidth_t bw = WIFI_BW20;
    esp_wifi_get_bandwidth(WIFI_IF_STA, &bw);
    wifi_phy_mode_t phyMode = WIFI_PHY_MODE_LR;
    bool phyValid = esp_wifi_sta_get_negotiated_phymode(&phyMode) == ESP_OK;
    uint16_t aid = 0;
    esp_wifi_sta_get_aid(&aid);
    char bssid[20] = "";
    if (apValid)
        snprintf(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x",
                    ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);

    char buf[700];
    snprintf(buf, sizeof(buf),
            R"("link":%d,"rssi":%d,"ch":%d,"bssid":"%s","aid":%d,"phy":%d,"ps":"%s","txPwr":%.2f,"proto":"%s","bw":%d,)"
            R"("upS":%u,"conn":%u,"disc":%u,"discReason":%u,"bcnTO":%u,"rssiLow":%u,"gotIP":%u,"lostIP":%u,)"
            R"("ping":{"target":"%s","ms":%u,"sent":%u,"recv":%u,"rttMs":%u,"maxRttMs":%u,"consecFail":%u,"maxConsecFail":%u,"lastOkAgoMs":%d,"recover":%u,"recoveries":%u},)"
            R"("logS":%u,"heap":{"int":%u,"intMin":%u,"intMaxBlk":%u,"spiram":%u})",
            apValid ? 1 : 0, apValid ? ap.rssi : 0, apValid ? ap.primary : 0, bssid, (int)aid,
            phyValid ? (int)phyMode : -1, psModeStr(ps), txPwrQdBm / 4.0, protocolStr(proto).c_str(), bw == WIFI_BW40 ? 40 : 20,
            (unsigned)((millis() - _connectedAtMs) / 1000), (unsigned)_connects.load(), (unsigned)_disconnects.load(),
            (unsigned)_lastDisconnectReason.load(), (unsigned)_beaconTimeouts.load(), (unsigned)_rssiLowEvents.load(),
            (unsigned)_gotIPs.load(), (unsigned)_lostIPs.load(),
            _pingTarget.c_str(), (unsigned)_pingIntervalMs, (unsigned)_pingSent.load(), (unsigned)_pingRecv.load(),
            (unsigned)_pingLastRttMs.load(), (unsigned)_pingMaxRttMs.load(), (unsigned)_pingConsecFail.load(),
            (unsigned)_pingMaxConsecFail.load(), _pingLastOkMs.load() ? (int)(millis() - _pingLastOkMs.load()) : -1,
            (unsigned)_pingRecoverCount, (unsigned)_pingRecoveries.load(), (unsigned)_logIntervalS,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return includeBraces ? "{" + String(buf) + "}" : String(buf);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Log a one-line summary of the link
void WiFiDiagnostics::logSummary() const
{
    wifi_ap_record_t ap = {};
    bool apValid = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    wifi_ps_type_t ps = WIFI_PS_NONE;
    esp_wifi_get_ps(&ps);
    int8_t txPwrQdBm = 0;
    esp_wifi_get_max_tx_power(&txPwrQdBm);
    LOG_I(MODULE_PREFIX, "link %d rssi %d ch %d bssid ..%02x:%02x ps %s txPwr %.1f disc %u(r%u) bcnTO %u | ping %u/%u rtt %u max %u fail %u maxFail %u okAgo %dms rec %u | heap %u min %u blk %u",
            apValid ? 1 : 0, apValid ? ap.rssi : 0, apValid ? ap.primary : 0, ap.bssid[4], ap.bssid[5],
            psModeStr(ps), txPwrQdBm / 4.0, (unsigned)_disconnects.load(), (unsigned)_lastDisconnectReason.load(),
            (unsigned)_beaconTimeouts.load(), (unsigned)_pingRecv.load(), (unsigned)_pingSent.load(),
            (unsigned)_pingLastRttMs.load(), (unsigned)_pingMaxRttMs.load(), (unsigned)_pingConsecFail.load(),
            (unsigned)_pingMaxConsecFail.load(), _pingLastOkMs.load() ? (int)(millis() - _pingLastOkMs.load()) : -1,
            (unsigned)_pingRecoveries.load(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Start pinging the gateway
/// @param pStaNetIf station network interface
void WiFiDiagnostics::startPing(esp_netif_t* pStaNetIf)
{
    // Get gateway address
    esp_netif_ip_info_t ipInfo = {};
    if ((esp_netif_get_ip_info(pStaNetIf, &ipInfo) != ESP_OK) || (ipInfo.gw.addr == 0))
        return;
    char gwStr[20];
    esp_ip4addr_ntoa(&ipInfo.gw, gwStr, sizeof(gwStr));
    _pingTarget = gwStr;

    // Config
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.target_addr.type = IPADDR_TYPE_V4;
    config.target_addr.u_addr.ip4.addr = ipInfo.gw.addr;
    config.count = ESP_PING_COUNT_INFINITE;
    config.interval_ms = _pingIntervalMs;
    config.timeout_ms = 1000;
    config.data_size = 32;
    esp_ping_callbacks_t cbs = {};
    cbs.cb_args = this;
    cbs.on_ping_success = pingSuccessCB;
    cbs.on_ping_timeout = pingTimeoutCB;

    // Start
    esp_ping_handle_t hdl = nullptr;
    if (esp_ping_new_session(&config, &cbs, &hdl) != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "startPing failed to create session");
        return;
    }
    _pingConsecFail = 0;
    esp_ping_start(hdl);
    _pingHandle = hdl;
    _pingActive = true;
    _pingActiveIntervalMs = _pingIntervalMs;
    LOG_I(MODULE_PREFIX, "gateway ping started target %s intervalMs %d", gwStr, (int)_pingIntervalMs);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Stop pinging the gateway
void WiFiDiagnostics::stopPing()
{
    if (_pingHandle)
    {
        esp_ping_stop((esp_ping_handle_t)_pingHandle);
        esp_ping_delete_session((esp_ping_handle_t)_pingHandle);
        _pingHandle = nullptr;
    }
    _pingActive = false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Gateway ping success (called on the ping task)
void WiFiDiagnostics::pingSuccessCB(void* hdl, void* args)
{
    WiFiDiagnostics* pThis = (WiFiDiagnostics*)args;
    uint32_t rttMs = 0;
    esp_ping_get_profile((esp_ping_handle_t)hdl, ESP_PING_PROF_TIMEGAP, &rttMs, sizeof(rttMs));
    pThis->_pingSent++;
    pThis->_pingRecv++;
    pThis->_pingLastRttMs = rttMs;
    if (rttMs > pThis->_pingMaxRttMs)
        pThis->_pingMaxRttMs = rttMs;
    pThis->_pingConsecFail = 0;
    pThis->_pingLastOkMs = millis();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Gateway ping timeout (called on the ping task)
void WiFiDiagnostics::pingTimeoutCB(void* hdl, void* args)
{
    WiFiDiagnostics* pThis = (WiFiDiagnostics*)args;
    pThis->_pingSent++;
    uint32_t consecFail = ++pThis->_pingConsecFail;
    if (consecFail > pThis->_pingMaxConsecFail)
        pThis->_pingMaxConsecFail = consecFail;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Disconnect from the AP (the disconnect handler then reconnects)
void WiFiDiagnostics::forceReconnect()
{
    LOG_I(MODULE_PREFIX, "forcing reconnect");
    esp_wifi_disconnect();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Power save mode as a string
const char* WiFiDiagnostics::psModeStr(wifi_ps_type_t ps)
{
    switch (ps)
    {
    case WIFI_PS_NONE: return "none";
    case WIFI_PS_MIN_MODEM: return "min";
    case WIFI_PS_MAX_MODEM: return "max";
    default: return "?";
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Protocol bitmap as a string
String WiFiDiagnostics::protocolStr(uint8_t bitmap)
{
    String str;
    if (bitmap & WIFI_PROTOCOL_11B) str += "b";
    if (bitmap & WIFI_PROTOCOL_11G) str += "g";
    if (bitmap & WIFI_PROTOCOL_11N) str += "n";
    if (bitmap & WIFI_PROTOCOL_LR) str += "L";
    return str;
}
