# devbin / devjson Publish Format Changes

## Status

All RaftCore changes have been implemented. The StatePublisher changes (section 9) require updates in the external repo that hosts `StatePublisher.cpp/.h`.

---

## Background

Two publish data sources are registered by `DeviceManager`:

- **devjson** — JSON text, used for all subscribers.
- **devbin** — compact binary, used for high-frequency, bandwidth-sensitive channels (BLE, etc.).

Issues identified in the original design:

1. **devbin had no outer envelope** — version, topic identity, and future extensibility were impossible without one.
2. **Topic name was passed in every devjson message** (`"_t":"devjson"`) but was silently unused in devbin.
3. **Topic names are long strings** carried in callbacks; an index is more efficient and consistent.
4. **No version field** in either format made future evolution ambiguous.
5. `registerDataSource` returned `bool`, so callers could not obtain the allocated topic index.

---

## Decisions

### devbin outer envelope

The first byte combines magic and version into a single byte:

| Byte value | Meaning |
|------------|---------|
| `0xDB` | devbin version 1 |
| `0xDC` | devbin version 2 |
| `0xDD` | devbin version 3 |
| … | … |

The upper nibble `0xD` serves as a constant identifier ("D" for device); the lower nibble is the version (B=11 decimal, so v1 is offset from `0xDA`). If a receiver sees a first byte outside the range `0xDB`–`0xDF` it should reject the frame.

Full envelope layout for version 1:

```
Byte  0     : magic+version  (0xDB = v1)
Byte  1     : topicIndex     (uint8, 0x00–0xFE; 0xFF = reserved/no topic)
Bytes 2…N   : per-device records (unchanged internal format, see below)
```

Per-device record (unchanged from current):

```
Bytes 0–1   : record length in bytes (uint16 big-endian, excludes these 2 bytes)
Byte  2     : busNumber | 0x80 if online  (lower 7 bits = bus number; 0 = directly connected)
Bytes 3–6   : address (uint32 big-endian)
Bytes 7–8   : deviceTypeIndex (uint16 big-endian)
Bytes 9…    : device poll data
```

Total envelope overhead: **2 bytes**.

> **Note on connection type (I2C / SPI / UART etc.)**: The per-device byte currently carries only the bus number. Encoding the connection/transport type would require repurposing bits in this byte or adding a new field, and would therefore require **devbin v2** (`0xDC`). This is explicitly out of scope for v1.

### devjson topic and version

- `"_t"` contains the **integer topicIndex** (not a name string) — consistent with devbin, avoids redundant string processing.
- `"_v":1` is the second field — its absence unambiguously means "pre-versioned, treat as legacy".

Example output:
```json
{"_t":2,"_v":1,"0":{...},"1":{...}}
```

Receivers that need to map index→name call the subscription API or the `pubtopics` endpoint (see StatePublisher section below).

### Topic index allocation

- `registerDataSource` return type changes from `bool` to `uint16_t`.
  - Returns the allocated topic index (0-based, stable for the session lifetime).
  - Returns `UINT16_MAX` on failure.
- The index is the 0-based position of the matching entry in the `StatePublisher`'s `_pubSources` list, ordered by registration. It is deterministic for a given firmware build.

### Callback signatures

Both typedefs in `RaftSysMod.h` change from passing `const char* topicName` to `uint16_t topicIndex`:

```cpp
// Before
typedef std::function<bool(const char* topicName, CommsChannelMsg& msg)> SysMod_publishMsgGenFn;
typedef std::function<void(const char* stateName, std::vector<uint8_t>& stateHash)> SysMod_stateDetectCB;

// After
typedef std::function<bool(uint16_t topicIndex, CommsChannelMsg& msg)> SysMod_publishMsgGenFn;
typedef std::function<void(uint16_t topicIndex, std::vector<uint8_t>& stateHash)> SysMod_stateDetectCB;
```

---

## Files Changed in RaftCore

### 1. `components/core/SysMod/RaftSysMod.h` ✅

- `SysMod_publishMsgGenFn` typedef: `const char* topicName` → `uint16_t topicIndex`.
- `SysMod_stateDetectCB` typedef: `const char* stateName` → `uint16_t topicIndex`.
- `registerDataSource` virtual method: returns `uint16_t` (default `UINT16_MAX`).

### 2. `components/core/SysManager/SysManagerIF.h` ✅

- `registerDataSource` pure virtual: returns `uint16_t`.

### 3. `components/core/SysManager/SysManager.h` ✅

- `registerDataSource` override declaration: returns `uint16_t`.

### 4. `components/core/SysManager/SysManager.cpp` ✅

- `registerDataSource` implementation propagates `uint16_t` from `pSysMod->registerDataSource(...)`.
- Returns `UINT16_MAX` on failure.

### 5. `components/core/DeviceManager/DeviceManager.h` ✅

- `getDevicesDataJSON(uint16_t topicIndex)` — single parameter, no topic name string.
- `getDevicesDataBinary(uint16_t topicIndex)` — unchanged from previous refactor.

### 6. `components/core/DeviceManager/DeviceManager.cpp` — `postSetup()` ✅

Lambdas receive `uint16_t topicIndex`. No topic name capture needed:

```cpp
getSysManager()->registerDataSource("Publish", "devjson",
    [this](uint16_t topicIndex, CommsChannelMsg& msg) {
        String statusStr = getDevicesDataJSON(topicIndex);
        msg.setFromBuffer((uint8_t*)statusStr.c_str(), statusStr.length());
        return true;
    },
    [this](uint16_t topicIndex, std::vector<uint8_t>& stateHash) {
        return getDevicesHash(stateHash);
    }
);
```

### 7. `components/core/DeviceManager/DeviceManager.cpp` — `getDevicesDataJSON()` ✅

- Signature: `String getDevicesDataJSON(uint16_t topicIndex) const`.
- Outputs `"_t"` as integer topicIndex and `"_v":1`:
  ```cpp
  jsonStr += "{\"_t\":";
  jsonStr += String(topicIndex);
  jsonStr += ",\"_v\":1";
  ```

### 8. `components/core/DeviceManager/DeviceManager.cpp` — `getDevicesDataBinary()` ✅

- Signature: `std::vector<uint8_t> getDevicesDataBinary(uint16_t topicIndex) const`.
- Prepends 2-byte envelope header:
  ```cpp
  binaryData.push_back(0xDB);  // magic + version 1
  binaryData.push_back(topicIndex <= 0xFE ? (uint8_t)topicIndex : 0xFF);
  ```

---

## StatePublisher Changes (external repo)

The `StatePublisher` is the concrete SysMod that implements `registerDataSource`. Its `registerDataSource` method already correctly returns a `uint16_t` topic index. The following additional changes are required:

### 9a. `registerDataSource` — already correct ✅

The current implementation searches `_pubSources` by topic name and returns the 0-based position index. This is correct. No changes needed.

### 9b. `apiSubscription` — update response to include topic index mapping

Currently `apiSubscription` returns only `{"rslt":"ok"}`. When a subscriber successfully subscribes, it needs to know which `topicIndex` values correspond to which topic names so it can decode `_t` in devjson and the envelope byte in devbin.

**Change**: In the `action == "update"` handler, after processing all `pubRecs`, build and include a `topics` array in the response:

```cpp
// After processing pubRecsToMod, add topic index info to response
String topicsJson = "[";
bool firstTopic = true;
for (const String& pubRecToMod : pubRecsToMod)
{
    RaftJson pubRecConf = pubRecToMod;
    String pubTopic = pubRecConf.getString("topic", pubRecConf.getString("name", "").c_str());
    PubSource* pPubSource = findPubSource(pubTopic);
    if (pPubSource)
    {
        if (!firstTopic) topicsJson += ",";
        topicsJson += "{\"name\":\"" + pubTopic + "\",\"idx\":" + String(pPubSource->_topicIndex) + "}";
        firstTopic = false;
    }
}
topicsJson += "]";

// Replace the simple bool response with one that includes topic info
Raft::setJsonBoolResult(cmdName.c_str(), respStr, true);
// Insert topics array before closing brace
respStr = respStr.substring(0, respStr.length() - 1) + ",\"topics\":" + topicsJson + "}";
```

Example response:
```json
{"rslt":"ok","topics":[{"name":"devjson","idx":2},{"name":"devbin","idx":3}]}
```

### 9c. Add `GET /api/pubtopics` endpoint

Add a REST endpoint that returns the full index→name mapping for all registered pub sources. This allows a receiver to bootstrap its topic lookup table at connection time without needing to subscribe first.

In `addRestAPIEndpoints`:
```cpp
endpointManager.addEndpoint("pubtopics", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
    std::bind(&StatePublisher::apiGetPubTopics, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
    "Get all registered publish topic names and their indexes");
```

Handler:
```cpp
RaftRetCode StatePublisher::apiGetPubTopics(const String& reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    String json = "{\"rslt\":\"ok\",\"topics\":[";
    bool first = true;
    for (const PubSource& ps : _pubSources)
    {
        if (!first) json += ",";
        json += "{\"name\":\"" + ps._pubTopic + "\",\"idx\":" + String(ps._topicIndex) + "}";
        first = false;
    }
    json += "]}";
    respStr = json;
    return RAFT_OK;
}
```

Example response:
```json
{"rslt":"ok","topics":[{"name":"MultiStatus","idx":0},{"name":"devjson","idx":2},{"name":"devbin","idx":3}]}
```

---

## Non-Goals / Out of Scope

- The per-device record internal format (busNumber, address, deviceTypeIndex, payload) is **unchanged** for v1. Encoding connection type (I2C/SPI/UART) would require devbin v2 (`0xDC`).
- The `getDevicesHash` function signature is unchanged; it ignores the topic parameter entirely.
- MQTT topic names (in `RaftMQTTClient`) are a separate concept and are not affected.

---

## Migration / Compatibility Notes

- Any existing devbin decoder must be updated to skip 2 header bytes before reading device records.
- Any existing devjson decoder must be updated: `_t` is now an integer, not a string. The `_v` field is new and should be tolerated by existing decoders.
- The topic index is session-stable but not persistent across firmware updates; receivers should re-request the index→name mapping (via `GET /api/pubtopics` or from the subscription response) on reconnection.
- `UINT16_MAX` (0xFFFF) is used as a sentinel for "no topic / failure"; topic indices 0–0xFFFE are valid.
- The topicIndex in the devbin envelope is stored as `uint8_t` (1 byte), so valid devbin topic indices are 0–254; 0xFF is reserved. This is sufficient given typical pubList sizes.
