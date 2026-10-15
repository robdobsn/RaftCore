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
        // A lock set by recovery is cleared when the connection is later lost (so any AP can then be joined). This
        // is done in loop() as the event task's stack is small. The disconnect caused by setting the lock is ignored
        if (_autoLockActive && Raft::isTimeout(millis(), _autoLockSetMs, AUTO_LOCK_SETTLE_MS))
            _autoLockClearPending = true;
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
void WiFiDiagnostics::loop(esp_netif_t* pStaNetIf, bool staConnectedWithIP, WiFiScanner& wifiScanner)
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

    // Recover if the gateway can't be reached although the station is connected. If the gateway has responded
    // (on any connection since startup) the normal threshold is used. Otherwise the AP may not be passing data
    // (e.g. joined at startup) or the gateway may not respond to pings - a longer threshold is used and only a
    // few attempts are made so a gateway which doesn't respond to pings doesn't cause repeated reconnection
    if ((_recoveryState == RecoveryState::IDLE) && _pingActive && (_pingRecoverCount > 0) &&
                (!_recoveryDone || Raft::isTimeout(millis(), _lastRecoveryMs, RECOVERY_MIN_INTERVAL_MS)))
    {
        bool gatewayKnown = _pingOkThisSession || _gatewayEverResponded;
        uint32_t threshold = gatewayKnown ? _pingRecoverCount : _pingRecoverCount * UNKNOWN_GATEWAY_THRESHOLD_MULT;
        if ((_pingConsecFail >= threshold) && (gatewayKnown || (_unknownGatewayRecoveries < UNKNOWN_GATEWAY_MAX_RECOVERIES)))
        {
            if (!gatewayKnown)
                _unknownGatewayRecoveries++;
            recoveryStart(wifiScanner);
        }
    }

    // Clear a lock set by recovery once its connection has been lost
    if (_autoLockClearPending.exchange(false) && _autoLockActive)
    {
        _lastRecoveryStr += " (lock cleared after disconnect reason " + String(_lastDisconnectReason.load()) + ")";
        setBssidLock(nullptr, true);
    }

    // Recovery waits for a scan to find another AP
    if (_recoveryState == RecoveryState::SCANNING)
    {
        WiFiScanner::ScanState scanState = wifiScanner.getScanState();
        bool scanFinished = (wifiScanner.getScanId() == _recoveryScanId) &&
                    ((scanState == WiFiScanner::ScanState::DONE) || (scanState == WiFiScanner::ScanState::FAILED));
        bool timedOut = Raft::isTimeout(millis(), _recoveryStartMs, RECOVERY_SCAN_TIMEOUT_MS);
        if (scanFinished || timedOut)
            recoveryComplete(wifiScanner, scanFinished && (scanState == WiFiScanner::ScanState::DONE));
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

    // Lock to a specific AP (e.g. one node of a mesh) or "any" to clear
    String bssidStr = options.getString("bssid", "");
    if (bssidStr.length() > 0)
    {
        uint8_t bssid[6] = {};
        bool lockToBssid = !bssidStr.equalsIgnoreCase("any");
        if (lockToBssid && !parseBssid(bssidStr, bssid))
        {
            errStr = "invalidBSSID";
            return false;
        }
        if (!setBssidLock(lockToBssid ? bssid : nullptr, true))
        {
            errStr = "setConfigFailed";
            return false;
        }
        _manualLockActive = lockToBssid;
        _autoLockActive = false;
    }

    // Gateway ping and recovery
    if (options.contains("pingMs"))
        _pingIntervalMs = options.getLong("pingMs", 0);
    if (options.contains("pingRecover"))
        _pingRecoverCount = options.getLong("pingRecover", 0);

    // Simulate the gateway not responding (testing)
    if (options.contains("simGwFail"))
        _simGatewayFail = options.getLong("simGwFail", 0);

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

    char lockStr[24] = "";
    if (_manualLockActive || _autoLockActive)
        snprintf(lockStr, sizeof(lockStr), "%s%s", _manualLockActive ? "manual " : "auto ", bssidToStr(_lockedBssid).c_str());

    char buf[1000];
    snprintf(buf, sizeof(buf),
            R"("link":%d,"rssi":%d,"ch":%d,"bssid":"%s","aid":%d,"phy":%d,"ps":"%s","txPwr":%.2f,"proto":"%s","bw":%d,)"
            R"("upS":%u,"conn":%u,"disc":%u,"discReason":%u,"bcnTO":%u,"rssiLow":%u,"gotIP":%u,"lostIP":%u,)"
            R"("ping":{"target":"%s","ms":%u,"sent":%u,"recv":%u,"rttMs":%u,"maxRttMs":%u,"consecFail":%u,"maxConsecFail":%u,"lastOkAgoMs":%d,"recover":%u,"recoveries":%u},)"
            R"("recov":{"switches":%u,"avoided":%u,"lock":"%s","last":"%s"},)"
            R"("logS":%u,"heap":{"int":%u,"intMin":%u,"intMaxBlk":%u,"spiram":%u})",
            apValid ? 1 : 0, apValid ? ap.rssi : 0, apValid ? ap.primary : 0, bssid, (int)aid,
            phyValid ? (int)phyMode : -1, psModeStr(ps), txPwrQdBm / 4.0, protocolStr(proto).c_str(), bw == WIFI_BW40 ? 40 : 20,
            (unsigned)((millis() - _connectedAtMs) / 1000), (unsigned)_connects.load(), (unsigned)_disconnects.load(),
            (unsigned)_lastDisconnectReason.load(), (unsigned)_beaconTimeouts.load(), (unsigned)_rssiLowEvents.load(),
            (unsigned)_gotIPs.load(), (unsigned)_lostIPs.load(),
            _pingTarget.c_str(), (unsigned)_pingIntervalMs, (unsigned)_pingSent.load(), (unsigned)_pingRecv.load(),
            (unsigned)_pingLastRttMs.load(), (unsigned)_pingMaxRttMs.load(), (unsigned)_pingConsecFail.load(),
            (unsigned)_pingMaxConsecFail.load(), _pingLastOkMs.load() ? (int)(millis() - _pingLastOkMs.load()) : -1,
            (unsigned)_pingRecoverCount, (unsigned)_pingRecoveries.load(),
            (unsigned)_apSwitches.load(), (unsigned)numAvoidedAPs(), lockStr, _lastRecoveryStr.c_str(), (unsigned)_logIntervalS,
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
    _pingOkThisSession = false;
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
    if ((pThis->_simGatewayFail == 2) || ((pThis->_simGatewayFail == 1) && pThis->_pingOkThisSession))
    {
        pingTimeoutCB(hdl, args);
        return;
    }
    uint32_t rttMs = 0;
    esp_ping_get_profile((esp_ping_handle_t)hdl, ESP_PING_PROF_TIMEGAP, &rttMs, sizeof(rttMs));
    pThis->_pingSent++;
    pThis->_pingRecv++;
    pThis->_pingLastRttMs = rttMs;
    if (rttMs > pThis->_pingMaxRttMs)
        pThis->_pingMaxRttMs = rttMs;
    pThis->_pingConsecFail = 0;
    pThis->_pingLastOkMs = millis();
    pThis->_pingOkThisSession = true;
    pThis->_gatewayEverResponded = true;
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
/// @brief Start recovery - the gateway can't be reached although the station is associated
/// @param wifiScanner scanner used to find another AP
void WiFiDiagnostics::recoveryStart(WiFiScanner& wifiScanner)
{
    _pingRecoveries++;
    _recoveryDone = true;
    _lastRecoveryMs = millis();

    // Record the AP which isn't passing data
    wifi_ap_record_t ap = {};
    bool apValid = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    memcpy(_failingBssid, ap.bssid, sizeof(_failingBssid));
    _failingSsid = apValid ? String((const char*)ap.ssid) : "";
    LOG_W(MODULE_PREFIX, "gateway %s unreachable for %d pings while associated with %s (rssi %d) - recovery %d",
                _pingTarget.c_str(), (int)_pingConsecFail.load(), bssidToStr(_failingBssid).c_str(),
                apValid ? ap.rssi : 0, (int)_pingRecoveries.load());
    logSummary();
    _pingConsecFail = 0;
    stopPing();

    // Look for another AP (not possible with a manual lock in place) - the scan is made while associated
    if (!apValid || _manualLockActive || !wifiScanner.scanStart())
    {
        recoveryComplete(wifiScanner, false);
        return;
    }
    _recoveryScanId = wifiScanner.getScanId();
    _recoveryStartMs = millis();
    _recoveryState = RecoveryState::SCANNING;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Complete recovery - join the strongest other AP with the same SSID (avoiding the failing AP for a while)
///        or reconnect if there isn't one
/// @param wifiScanner scanner with the results of the recovery scan
/// @param scanOk true if the scan completed
void WiFiDiagnostics::recoveryComplete(WiFiScanner& wifiScanner, bool scanOk)
{
    _recoveryState = RecoveryState::IDLE;
    avoidAP(_failingBssid);

    // Find the strongest other AP with the same SSID
    const WiFiScanner::WiFiScanResult* pBest = nullptr;
    uint8_t bestBssid[6] = {};
    if (scanOk && (_failingSsid.length() > 0))
    {
        for (const WiFiScanner::WiFiScanResult& result : wifiScanner.getResults())
        {
            uint8_t bssid[6] = {};
            // (the scanner's SSID strings are fixed length with null padding so compare as C strings)
            if ((strcmp(result.ssid.c_str(), _failingSsid.c_str()) != 0) || !parseBssid(result.bssid, bssid) || isAPAvoided(bssid) ||
                        (result.rssi < RECOVERY_MIN_ALT_RSSI))
                continue;
            if (!pBest || (result.rssi > pBest->rssi))
            {
                pBest = &result;
                memcpy(bestBssid, bssid, sizeof(bestBssid));
            }
        }
    }

    // Join it, or reconnect (clearing any lock to the failing AP)
    if (pBest)
    {
        _lastRecoveryStr = "switched " + bssidToStr(_failingBssid) + " to " + pBest->bssid + " rssi " + String(pBest->rssi);
        LOG_W(MODULE_PREFIX, "recovery joining %s (rssi %d) and avoiding %s for %ds", pBest->bssid.c_str(), pBest->rssi,
                    bssidToStr(_failingBssid).c_str(), (int)(AVOID_AP_MS / 1000));
        if (setBssidLock(bestBssid, true))
        {
            _autoLockActive = true;
            _autoLockSetMs = millis();
            _apSwitches++;
            return;
        }
    }
    _lastRecoveryStr = "reconnect (" + String(scanOk ? "no other AP" : (_manualLockActive ? "manual lock" : "scan failed")) + ")";
    LOG_W(MODULE_PREFIX, "recovery %s", _lastRecoveryStr.c_str());
    if (_autoLockActive)
    {
        _autoLockActive = false;
        setBssidLock(nullptr, true);
    }
    else
    {
        forceReconnect();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Lock the station to an AP (or clear the lock) - the lock is not stored in flash so a restart clears it
/// @param pBssid BSSID to lock to or nullptr to clear the lock
/// @param reconnect true to disconnect and connect with the new setting (otherwise used when disconnected)
/// @return true if the setting was applied
bool WiFiDiagnostics::setBssidLock(const uint8_t* pBssid, bool reconnect)
{
    wifi_config_t staConfig = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &staConfig) != ESP_OK)
        return false;
    staConfig.sta.bssid_set = pBssid != nullptr;
    if (pBssid)
    {
        memcpy(staConfig.sta.bssid, pBssid, sizeof(staConfig.sta.bssid));
        memcpy(_lockedBssid, pBssid, sizeof(_lockedBssid));
    }
    else
    {
        _autoLockActive = false;
    }

    // The config can't be changed while connecting so disconnect first
    if (reconnect)
        esp_wifi_disconnect();
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &staConfig);
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    LOG_I(MODULE_PREFIX, "bssid lock %s%s", pBssid ? bssidToStr(pBssid).c_str() : "cleared", err == ESP_OK ? "" : " FAILED");
    if (reconnect)
        esp_wifi_connect();
    return err == ESP_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Avoid an AP (in recovery) for a while
void WiFiDiagnostics::avoidAP(const uint8_t* pBssid)
{
    // Reuse the entry for this AP, an expired entry or the one expiring soonest
    uint32_t nowMs = millis();
    int idx = 0;
    for (uint32_t i = 0; i < MAX_AVOIDED_APS; i++)
    {
        AvoidedAP& rec = _avoidedAPs[i];
        if (rec.valid && (memcmp(rec.bssid, pBssid, 6) == 0)) { idx = i; break; }
        if (!rec.valid || ((int32_t)(rec.untilMs - nowMs) <= 0)) { idx = i; continue; }
        if (_avoidedAPs[idx].valid && ((int32_t)(rec.untilMs - _avoidedAPs[idx].untilMs) < 0)) idx = i;
    }
    memcpy(_avoidedAPs[idx].bssid, pBssid, 6);
    _avoidedAPs[idx].untilMs = nowMs + AVOID_AP_MS;
    _avoidedAPs[idx].valid = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if an AP is being avoided
bool WiFiDiagnostics::isAPAvoided(const uint8_t* pBssid) const
{
    uint32_t nowMs = millis();
    for (const AvoidedAP& rec : _avoidedAPs)
    {
        if (rec.valid && ((int32_t)(rec.untilMs - nowMs) > 0) && (memcmp(rec.bssid, pBssid, 6) == 0))
            return true;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Number of APs currently avoided
uint32_t WiFiDiagnostics::numAvoidedAPs() const
{
    uint32_t count = 0;
    uint32_t nowMs = millis();
    for (const AvoidedAP& rec : _avoidedAPs)
    {
        if (rec.valid && ((int32_t)(rec.untilMs - nowMs) > 0))
            count++;
    }
    return count;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Parse a BSSID string (xx:xx:xx:xx:xx:xx)
bool WiFiDiagnostics::parseBssid(const String& bssidStr, uint8_t* pBssid)
{
    unsigned int b[6] = {};
    if (sscanf(bssidStr.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
        return false;
    for (int i = 0; i < 6; i++)
        pBssid[i] = (uint8_t)b[i];
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief BSSID as a string
String WiFiDiagnostics::bssidToStr(const uint8_t* pBssid)
{
    char str[20];
    snprintf(str, sizeof(str), "%02x:%02x:%02x:%02x:%02x:%02x", pBssid[0], pBssid[1], pBssid[2], pBssid[3], pBssid[4], pBssid[5]);
    return str;
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
