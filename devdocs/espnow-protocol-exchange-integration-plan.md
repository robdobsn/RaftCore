# ESPNow ProtocolExchange Integration Plan

Rob Dobson / GitHub Copilot investigation, 2026-05-03

## Goal

Add ESPNow as a Raft communications transport so it can carry the same ProtocolExchange traffic currently carried by BLE, WebSockets, and serial links. The integration should avoid adding ESPNow transport code to the core RaftCore component if possible, and ESPNow code and dependencies should only be compiled when deliberately selected by a SysType through `features.cmake` and/or `sdkconfig.defaults`.

## Summary Recommendation

Implement ESPNow as a new optional SysMod in RaftSysMods, not as a new RaftCore protocol. The SysMod should register one or more CommsCore channels, feed received ESPNow payload bytes into `CommsCoreIF::inboundHandleMsg()`, and send outbound encoded bytes from the CommsCore channel send callback. This matches how BLE, WebSockets, CommandSerial, CommandSocket, and SerialConsole already plug into ProtocolExchange.

Use the existing RaftCore protocol codecs, initially `RICSerial`, as the on-air payload format. `RICSerial` is the best default because it is already the BLE and serial stream protocol, and its HDLC framing can tolerate the transport delivering chunks smaller than a full logical message. ESPNow's small per-packet payload limit makes this important. `RICFrame` can be allowed later for carefully bounded single-datagram messages, but should not be the default.

Compile gating should be stronger than the current BLE pattern. BLE source files are compiled unconditionally in RaftSysMods but almost all code is guarded by `CONFIG_BT_ENABLED`. For ESPNow, prefer conditional CMake source/dependency inclusion as well as source-level guards, so firmware that does not select ESPNow does not compile or link the transport code or require ESPNow-specific component dependencies.

Initial implementation decisions:

- Require `NetMan` for the first implementation so ESPNow reuses existing WiFi initialization and avoids duplicate WiFi ownership.
- Support configured peers first, but design the peer/channel table so dynamic peer learning can be added as a staged feature without changing the CommsCore model.
- Allow unencrypted ESPNow for the first development pass.
- Preserve a path to file/stream support by using `RICSerial` and explicit transport fragmentation from the beginning, then add ACK/retry before treating streams as production-ready.
- Defer broadcast discovery to a later phase.
- Target ESP-IDF 6.0+ and Raft-to-Raft peers only.

## Existing Architecture

### ProtocolExchange is protocol-codec registration, not transport ownership

`ProtocolExchange::addCommsChannels()` registers three protocol codecs with `CommsCoreIF`:

- `RICSerial`
- `RICFrame`
- `RICJSON`

Each codec receives raw transport bytes from a CommsChannel and converts them into `CommsChannelMsg` instances. ProtocolExchange then handles those messages as RICREST commands, bridged RICREST payloads, raw command frames, file stream blocks, or publish/report traffic.

Important details:

- Transport code should not call ProtocolExchange directly.
- Transport code should register channels with CommsCore.
- The selected protocol name on the channel determines which codec CommsCore lazily creates for that channel.
- The codec callback feeds decoded `CommsChannelMsg` into `ProtocolExchange::processEndpointMsg()`.
- Responses are routed by channel ID back through `CommsCoreIF::outboundHandleMsg()`.

This makes ESPNow a natural transport-layer addition rather than a ProtocolExchange rewrite.

### CommsChannelManager owns the transport/codecs boundary

`CommsChannelManager::registerChannel()` creates a channel with:

- protocol name, such as `RICSerial`
- interface name, such as `BLE`, `ws`, `CommandSerial`, or `SerialConsole`
- channel name
- outbound send callback
- outbound readiness callback
- optional `CommsChannelSettings`

Inbound path:

1. Transport receives bytes.
2. Transport calls `CommsCoreIF::inboundHandleMsg(channelID, data, len)`.
3. CommsChannel queues raw bytes.
4. In CommsChannelManager loop, the protocol codec consumes queued bytes.
5. The codec emits `CommsChannelMsg` to ProtocolExchange.

Outbound path:

1. ProtocolExchange or StatePublisher creates `CommsChannelMsg` with a target channel ID.
2. CommsCore queues or immediately handles the message.
3. The channel's protocol codec encodes it.
4. The encoded bytes are passed to the transport's send callback.

For ESPNow, the SysMod only needs to implement this channel boundary cleanly.

## Existing Transport Patterns

### BLE

BLE is in RaftSysMods under `components/BLEManager`.

Build/runtime shape:

- `BLEManager` is only included/registered from `RegisterSysMods.h` when `CONFIG_BT_ENABLED` is true.
- BLE implementation files include `sdkconfig.h` and guard most code with `#ifdef CONFIG_BT_ENABLED`.
- RaftSysMods currently still lists BLE source files and the `bt` dependency unconditionally in `CMakeLists.txt`.

Channel registration:

- `BLEManager::addCommsChannels()` calls `_gapServer.registerChannel(commsCoreIF)`.
- `BLEGapServer::registerChannel()` registers a channel with protocol `RICSerial`, interface `BLE`, channel `BLE`.
- Channel settings use the GATT max packet length as inbound/outbound block sizing.

Inbound path:

- The central writes chunks to the BLE command characteristic.
- `BLEGattServer::commandCharAccess()` extracts the write payload.
- `BLEGapServer::gattAccessCallback()` calls `_pCommsCoreIF->inboundHandleMsg(_commsChannelID, payloadbuffer, payloadlength)`.
- The channel's `RICSerial` codec reconstructs and decodes the HDLC message.

Outbound path:

- BLE's channel send callback is `BLEGapServer::sendBLEMsg()`.
- `BLEGattOutbound` stores command and publish messages in separate queues.
- Outbound data is split into chunks sized to the negotiated MTU.
- Command and publish queues can use different reliability modes: indications for acknowledged sends, notifications for lower-latency publishes.
- Outbound readiness reports no connection when BLE is not initialized or not connected, letting CommsChannelManager discard queued messages instead of blocking forever.

ESPNow should copy the separation of command and publish queues, connection/no-peer readiness behavior, and chunked sending model, while replacing BLE-specific MTU/indication logic with ESPNow peer send callbacks and packet-size limits.

### WebSockets

WebSocket support is in RaftWebServer, outside RaftCore.

Build/runtime shape:

- `RegisterWebServer.h` only registers the WebServer SysMod when networking is enabled through WiFi or Ethernet config symbols.
- WebServer creation depends on the SysType including the RaftWebServer component in `features.cmake`.

Channel registration:

- `WebServer::postSetup()` calls `webSocketSetup()` after CommsCore is available.
- Each configured websocket endpoint creates a `RaftWebHandlerWS`.
- Each possible connection slot gets its own CommsCore channel.
- The channel protocol comes from JSON key `pcol`, defaulting to `RICSerial`.
- The interface name comes from JSON key `pfix`, defaulting to `ws`.

Inbound path:

- `RaftWebResponderWS` receives websocket binary/text payloads.
- It calls the inbound CommsCore callbacks provided by `RaftWebHandlerWS`.
- The chosen channel codec decodes payload bytes.

Outbound path:

- The channel send callback sends encoded bytes through `_raftWebServer.sendBufferOnChannel()` for the matching channel ID.
- Readiness can be cheap connection-state testing (`isChannelConnected`) or deeper send-buffer readiness.

ESPNow should borrow the idea of multiple logical channel slots. For ESPNow, each peer MAC can map to one channel, either preconfigured or learned dynamically.

### Serial and overAscii

There are two relevant serial patterns.

`CommandSerial`:

- Configured ports are created from `CommandSerial/ports`.
- Each port has a `protocol`, defaulting in examples to `RICSerial`.
- `addCommsChannels()` registers one channel per serial port.
- The loop reads raw UART bytes and calls `inboundHandleMsg()` for that port channel.
- Outbound send callback writes encoded bytes directly to the matching UART.

`SerialConsole`:

- Registers a channel using its configured `protocol`, default `RICSerial`.
- The console still accepts plain line-oriented REST commands for humans.
- Protocol bytes are tunneled through `ProtocolOverAscii`, which encodes arbitrary binary into bytes with the MSB set so they can coexist with ordinary terminal ASCII.
- Received MSB-set bytes are decoded back to raw protocol bytes before `inboundHandleMsg()`.
- Outbound encoded protocol bytes are passed through `ProtocolOverAscii::encodeFrame()` before writing to the terminal.

ESPNow does not need overAscii because ESPNow payloads are binary-safe. The useful lesson is that the transport can add an extra low-level framing/escaping layer outside the RaftCore protocol codec if the physical medium requires it.

## ESPNow Transport Constraints

ESPNow is datagram-oriented, not stream-oriented. Practical implications:

- ESPNow requires WiFi driver initialization and a WiFi mode/channel before `esp_now_init()`.
- ESPNow payload size is limited. Classic ESP-NOW v1 payloads are 250 bytes. ESP-NOW v2 supports larger payloads, but both ends must support and agree to use them. Since this design targets Raft-to-Raft firmware on ESP-IDF 6.0+, the implementation can use IDF 6.0 APIs/macros where available, but should still default to 250-byte payloads until peer capability is known or configured.
- ESPNow send completion is asynchronous through a send callback.
- ESPNow receive callback may run in WiFi task context; callbacks should copy data into queues and return quickly.
- Peer MAC, WiFi channel, interface, encryption key, and broadcast/unicast behavior matter.
- Encrypted unicast requires peer configuration and key management.
- Broadcast is useful for discovery and announcements but not for secure command/response.
- The radio is shared with WiFi STA/AP and BLE, so channel selection and coexistence should be explicit.

Because ProtocolExchange expects request/response and file/stream behavior to work over a channel ID, the ESPNow SysMod should model peers as channels and preserve channel identity from receive through response.

## Proposed Architecture

### New RaftSysMods component area

Add a new optional SysMod under RaftSysMods:

```text
components/ESPNowManager/
    ESPNowManager.h/.cpp
    ESPNowConfig.h
    ESPNowPeer.h/.cpp
    ESPNowOutbound.h/.cpp
    ESPNowPacketHeader.h
```

Suggested SysMod registration name: `ESPNow` or `ESPNowMan`. `ESPNow` reads better in `SysTypes.json`, while `ESPNowMan` matches `BLEMan` and `NetMan`. The examples below use `ESPNow`.

Responsibilities:

- Parse SysType config.
- Ensure WiFi/ESPNow prerequisites are satisfied.
- Register one channel per configured or learned peer.
- Maintain peer table and channel ID mapping.
- Queue received datagrams from the ESPNow callback.
- Feed payload fragments to CommsCore from the SysMod loop.
- Queue outbound encoded bytes per peer.
- Fragment outbound encoded protocol bytes into ESPNow datagrams.
- Pace sends so only an acceptable number are in flight.
- Expose status/debug REST endpoints.

### Keep RaftCore unchanged initially

No new ProtocolExchange behavior is required for basic command/response/publish support. ESPNow should use:

- `CommsCoreIF::registerChannel()`
- `CommsCoreIF::inboundHandleMsg()`
- `CommsCoreIF::outboundCanAccept()` behavior
- existing `RICSerial` codec
- existing `StatePublisher` subscriptions
- existing REST API and file stream mechanisms

Possible future RaftCore improvements, if needed later:

- A generic datagram-fragmentation helper for transports with small MTUs.
- A way for StatePublisher SysType config to create startup subscriptions by interface/protocol/channel name, if that is still desired. Current StatePublisher code supports dynamic API/programmatic subscriptions, but the older `Publish/pubList/rates` JSON shown in test data does not appear to be implemented in the current StatePublisher.

### Channel model

Recommended model: one CommsCore channel per peer MAC address.

For configured peers:

- Register channels during `addCommsChannels()`.
- Channel name can be the configured peer name.
- Interface name should be `ESPNow` by default.
- Protocol defaults to `RICSerial`.

For learned peers:

- Option A: reserve a fixed number of peer slots from config and register all channels at startup, similar to WebSocket connection slots.
- Option B: dynamically register channels when a peer is learned.

Option A is safer with the current lifecycle because SysMods get `addCommsChannels()` once during setup and other modules may expect stable channels. Dynamic registration probably works because CommsCore exposes `registerChannel()`, but startup slot reservation is easier to reason about and mirrors WebSocket connection slots.

Recommended first implementation:

- Support configured peers first.
- Optionally support `learnPeers` using a fixed `maxPeers` slot pool.
- Reject or ignore traffic from unknown peers unless discovery/learning is explicitly enabled.

### Protocol choice

Default protocol: `RICSerial`.

Why:

- It is already used by BLE and most command transports.
- It includes HDLC framing, so a full logical message can span multiple underlying ESPNow datagrams.
- It tolerates receiving a stream of fragments rather than a single complete logical frame per transport packet.
- It preserves existing command/response, bridge, file stream, and publish behavior.

Alternative protocol: `RICFrame`.

Use only when every encoded message fits within one ESPNow payload, or when the ESPNow SysMod adds its own reliable fragmentation and reassembly before calling CommsCore. `RICFrame` has lower overhead but does not provide a stream frame boundary beyond the single buffer passed to the codec.

Do not use `RICJSON` as the default. It only turns a raw JSON command into a `RAWCMDFRAME` command and does not preserve the richer RICREST/message-type behavior.

### ESPNow packet framing

Even when using `RICSerial`, ESPNow datagrams should carry a small transport header before the protocol bytes. This helps with peer management, duplicate/loss detection, and future compatibility.

Proposed header version 1:

```text
byte 0: magic 0x52 ('R')
byte 1: version 1
byte 2: flags
        bit 0: start of encoded protocol message
        bit 1: end of encoded protocol message
        bit 2: ack requested
        bit 3: ack frame
byte 3: header length
byte 4: sequence number low byte
byte 5: sequence number high byte
byte 6: fragment index
byte 7: fragment count, or 0 if streaming/no count
bytes 8..n: encoded protocol bytes or ack payload
```

For first implementation, this header can be minimal:

- Use sequence number for logging and duplicate suppression.
- Set start/end flags around chunks from each encoded `CommsChannelMsg`.
- Feed the payload bytes directly to the `RICSerial` codec in receive order.
- Do not block initial command/response support on transport-level ACKs.

Later, reliability can be added with missing-fragment detection and retransmission. For basic ProtocolExchange command/response, RICREST responses already give end-to-end feedback, but ESPNow packet loss can still drop the transport frame.

### Outbound queueing and fragmentation

Use an `ESPNowOutbound` helper similar in role to `BLEGattOutbound`.

Recommended behavior:

- Maintain separate command/response and publish queues per peer or per channel.
- Prefer command/response queue over publish queue.
- Drop or throttle publish messages when queues are full.
- Keep non-publish messages queued unless no peer/no connection is known.
- Limit one in-flight ESPNow send per peer until send callback reports completion. This avoids uncontrolled packet bursts and preserves fragment order.
- Fragment the encoded protocol buffer into datagrams sized to `maxPayloadLen - headerLen`.
- Use `esp_now_send(peerMac, packet, packetLen)`.
- On send callback, advance the peer's queue and wake the sender task or service loop.

Use a task only if needed. BLE supports either loop-driven sending or a task. ESPNow can start loop-driven for simplicity, then add an optional task when testing shows callback/loop pacing is insufficient.

### Inbound receive handling

ESPNow receive callback should do the minimum:

1. Copy source MAC, RSSI/channel metadata if available, and payload bytes into an internal queue.
2. Return immediately.

In `ESPNowManager::loop()`:

1. Drain a bounded number of queued datagrams per loop.
2. Validate header/version/length.
3. Find peer/channel by source MAC.
4. Learn peer if enabled and a free slot exists.
5. Feed the protocol payload bytes into `getCommsCore()->inboundHandleMsg(channelID, payload, payloadLen)`.

This follows the existing CommsChannel inbound queue pattern and avoids doing Raft processing in the WiFi callback context.

### Peer configuration

Example SysTypes JSON:

```json
"ESPNow": {
    "enable": 1,
    "protocol": "RICSerial",
    "interface": "ESPNow",
    "wifiMode": "sta",
    "channel": 6,
    "allowBroadcast": 0,
    "learnPeers": 0,
    "maxPeers": 4,
    "maxPayload": 250,
    "txQueueMax": 12,
    "pubQueueMax": 8,
    "taskEnable": 0,
    "peers": [
        {
            "name": "controller",
            "mac": "24:6F:28:AA:BB:CC",
            "channel": 6,
            "encrypt": 0
        }
    ]
}
```

Potential settings:

- `enable`: runtime SysMod enable.
- `protocol`: default `RICSerial`.
- `interface`: default `ESPNow`.
- `wifiMode`: `sta`, `ap`, or `apsta`. Usually match NetworkSystem mode.
- `channel`: explicit ESPNow/WiFi channel. Required if there is no normal WiFi connection managing channel.
- `peers`: configured peer list.
- `learnPeers`: learn source MACs into reserved slots.
- `maxPeers`: fixed channel slots for learned peers.
- `allowBroadcast`: permit broadcast sends/discovery.
- `pmk`: optional primary master key.
- `lmk`: optional per-peer local master key.
- `txQueueMax`: non-publish queue length.
- `pubQueueMax`: publish queue length.
- `minMsBetweenSends`: optional pacing.
- `taskEnable`, `taskCore`, `taskPriority`, `taskStack`: optional worker task settings.
- `maxPayload`: default 250 until capability/config confirms a larger ESP-NOW v2 payload is safe.

Payload-size note: the effective ESPNow payload size is not only an IDF-version choice. It depends on what the local ESP-IDF WiFi/ESPNow stack exposes, what the peer firmware supports, and possibly target/chip support in Espressif's implementation. Because this project only needs Raft-to-Raft communication and can target ESP-IDF 6.0+, a later capability/discovery frame can advertise each peer's supported payload size. Until then, 250 bytes is the safest interoperable default and only costs a little extra fragmentation.

### REST/debug endpoints

Useful endpoints:

- `espnow/status`: enabled, initialized, channel, peer count, queue depths, last send status.
- `espnow/peers`: peer table with name, MAC, channel ID, RSSI/last RX age, TX/RX counters.
- `espnow/addpeer?...`: optional runtime peer add for development.
- `espnow/delpeer?...`: optional runtime peer remove for development.

Keep mutation endpoints optional; static config first is simpler and safer.

## Build and Compile Gating

### Desired gates

There are two separate decisions:

1. The project includes RaftSysMods at all through `RAFT_COMPONENTS` in `features.cmake`.
2. RaftSysMods includes ESPNow sources/dependencies through an ESPNow-specific CMake flag and/or sdkconfig symbol.

Recommended CMake flag:

```cmake
set(RAFT_SYSMODS_ENABLE_ESPNOW ON)
```

Recommended sdkconfig defaults:

```text
CONFIG_RAFT_SYSMODS_ENABLE_ESPNOW=y
CONFIG_ESP_WIFI_ENABLED=y
CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM=0
```

The exact sdkconfig symbols should be verified against the ESP-IDF version in use. ESPNow itself is part of the ESP WiFi component; many projects only need WiFi enabled plus optional `CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM` for encrypted peers.

### Recommended RaftSysMods CMake change

RaftSysMods currently does this for BLE:

- `bt` is always in `RAFT_SYSMODS_REQUIRES`.
- BLE source files are always in `SRCS`.
- code and registration are guarded by `CONFIG_BT_ENABLED`.

For ESPNow, prefer this shape:

```cmake
set(RAFT_SYSMODS_SRCS
    ...existing non-ESPNow sources...
)

set(RAFT_SYSMODS_INCLUDE_DIRS
    ...existing include dirs...
)

set(RAFT_SYSMODS_REQUIRES RaftCore app_update esp_http_client esp_ringbuf)

if(RAFT_SYSMODS_ENABLE_BLE OR CONFIG_BT_ENABLED)
    list(APPEND RAFT_SYSMODS_REQUIRES bt)
    list(APPEND RAFT_SYSMODS_SRCS
        "components/BLEManager/BLEAdvertDecoder.cpp"
        ...)
    list(APPEND RAFT_SYSMODS_INCLUDE_DIRS "components/BLEManager")
endif()

if(RAFT_SYSMODS_ENABLE_ESPNOW)
    list(APPEND RAFT_SYSMODS_REQUIRES esp_wifi)
    list(APPEND RAFT_SYSMODS_SRCS
        "components/ESPNowManager/ESPNowManager.cpp"
        "components/ESPNowManager/ESPNowOutbound.cpp"
        "components/ESPNowManager/ESPNowPeer.cpp")
    list(APPEND RAFT_SYSMODS_INCLUDE_DIRS "components/ESPNowManager")
    add_compile_definitions(RAFT_SYSMODS_ENABLE_ESPNOW=1)
endif()
```

Because ESP-IDF CMake may not have all `CONFIG_*` values available early enough for component dependency resolution, the `features.cmake` flag is the more reliable source-selection mechanism. Source files should still guard ESP-IDF-specific code with `#if defined(ESP_PLATFORM) && defined(RAFT_SYSMODS_ENABLE_ESPNOW)` so Linux and unit builds remain clean.

### RegisterSysMods gating

Add ESPNow includes and registration only when the compile flag is set:

```cpp
#if defined(RAFT_SYSMODS_ENABLE_ESPNOW) && defined(ESP_PLATFORM)
#include "ESPNowManager.h"
#endif

...

#if defined(RAFT_SYSMODS_ENABLE_ESPNOW) && defined(ESP_PLATFORM)
sysManager.registerSysMod("ESPNow", ESPNowManager::create, false, "NetMan");
#endif
```

The first implementation should depend on `NetMan`. This deliberately reuses NetworkSystem WiFi initialization and avoids duplicate event loop/WiFi driver ownership. A later standalone ESPNow-only mode can still be added if a future firmware target needs ESPNow without IP networking.

## WiFi/Network Interaction

ESPNow needs WiFi started and on the same channel as peers. There are two viable modes.

### Mode A: ESPNow depends on NetMan

Use this for the first implementation.

Pros:

- Reuses existing `NetworkSystem::setup()` and `esp_wifi_init()`.
- Avoids duplicate event loop and WiFi driver setup.
- Fits existing `RegisterSysMods` dependency model.

Cons:

- If STA connects to an AP, the channel follows the AP and may not match configured ESPNow peers.
- SysTypes that want ESPNow but no IP networking still carry NetMan/network code.
- ESPNow setup must wait until WiFi is initialized.

### Mode B: ESPNow standalone WiFi init

Defer this until a product needs ESPNow without NetMan/IP networking.

Pros:

- ESPNow can be selected without WebServer/NetMan.
- More minimal for ESPNow-only firmware.

Cons:

- Must avoid conflicts if NetMan is also present.
- Need clear ownership of `esp_event_loop_create_default()`, `esp_netif_init()`, `esp_wifi_init()`, `esp_wifi_set_mode()`, `esp_wifi_start()`, and `esp_wifi_deinit()`.
- More edge cases with STA/AP coexistence.

Recommended plan: design ESPNowManager with a small WiFi ownership enum so Mode B remains possible later:

- `wifiOwner: "netman"` default
- `wifiOwner: "self"` future option
- `wifiOwner: "external"` for apps that initialize WiFi outside the SysMod

Implement only `netman` initially.

## Publishing and Subscriptions

Because ESPNow channels are ordinary CommsCore channels, publish messages should work through the existing StatePublisher once subscriptions target the ESPNow channel ID.

For dynamic clients, a peer can send the normal `subscription` REST command over ESPNow, and ProtocolExchange will route it through the peer's channel ID. Responses and subsequent publish messages then target the correct channel.

For static startup subscriptions, there are two options:

1. ESPNowManager creates programmatic subscriptions in `postSetup()` after channels exist, using `SysManagerIF` to find the `Publish` SysMod and `StatePublisher::createSubscription()` if accessible through a suitable interface.
2. Add or restore StatePublisher support for config-driven publish lists mapping `if` + `protocol` to a channel ID via `CommsCoreIF::getChannelIDByName()`.

Current code strongly supports dynamic/API subscriptions. Static config support should be treated as a separate small feature if required.

## Security

Initial safe defaults:

- Unknown peers ignored unless `learnPeers` is enabled.
- Broadcast receive can be accepted for discovery only, not privileged command execution, unless explicitly enabled.
- No encryption by default for the first development pass.
- Add per-peer LMK/PMK support before using ESPNow for sensitive commands.
- Log peer MACs and reject reasons at debug level.

ESPNow encryption limits are sdkconfig-dependent, so the plan should document the required encryption slot count when encrypted peers are configured.

## Reliability Model

Minimum viable reliability:

- Use `RICSerial` HDLC framing.
- Send fragments sequentially per peer.
- Use ESPNow send callback to pace the next fragment.
- Treat send callback failure as queue failure and expose counters.
- Rely on RICREST command/response for application-level success.

Known limitations:

- A lost ESPNow datagram can drop a complete HDLC frame.
- There is no retransmission in the initial design.
- File streams may be unreliable without transport ACK/retry, but the initial fragmentation design should not preclude file/stream operation later.

Recommended staged reliability improvements:

1. Add sequence/fragment counters and duplicate/drop counters.
2. Add optional ACK frames for non-publish messages.
3. Add retransmit with timeout and max retry count.
4. Keep publish messages best-effort by default.
5. Gate file upload/download support behind reliable mode or document it as experimental over ESPNow.

## Implementation Phases

### Phase 1: Skeleton and build gating

- Add `RAFT_SYSMODS_ENABLE_ESPNOW` flag support in RaftSysMods CMake.
- Add ESPNowManager files under RaftSysMods but include them only when the flag is ON.
- Add conditional include/register in `RegisterSysMods.h`.
- Add SysType example snippets for `features.cmake`, `sdkconfig.defaults`, and `SysTypes.json`.
- Build with flag OFF and verify no ESPNow files are compiled.
- Build with flag ON and empty disabled config to verify the component compiles.

### Phase 2: Configured peers and basic command/response

- Parse peer config.
- Initialize ESPNow after NetMan has WiFi ready.
- Register one channel per configured peer.
- Add peers with `esp_now_add_peer()`.
- Implement receive callback -> queue -> loop -> `inboundHandleMsg()`.
- Implement outbound queue -> fragment -> `esp_now_send()`.
- Use `RICSerial` protocol by default.
- Test with simple RICREST command/response.

### Phase 3: Publish support and diagnostics

- Add command and publish queues.
- Add no-peer/no-connection readiness behavior.
- Add status/debug endpoints.
- Test dynamic `subscription` API over ESPNow.
- Measure queue depths, send callback latency, loss counters, and heap use.

### Phase 4: Dynamic peer learning

- Add optional fixed peer slots for learned MACs.
- Register a stable channel pool at startup so learned peers can be assigned channel IDs without changing the rest of the CommsCore flow.
- Accept traffic from unknown peers only when `learnPeers` is enabled.
- Add peer aging, replacement policy, and status reporting.

### Phase 5: Broadcast discovery

- Add broadcast discovery frame type.
- Add optional peer persistence if needed.
- Keep command processing disabled for unknown/broadcast peers unless explicitly allowed.

### Phase 6: Reliability and file streams

- Add transport ACK/retry for non-publish messages.
- Add fragment reassembly/validation if moving beyond streaming HDLC chunks.
- Test file upload/download and firmware update behavior.
- Define maximum recommended file stream block sizes for ESPNow.

## Testing Plan

Build tests:

- `RAFT_SYSMODS_ENABLE_ESPNOW` OFF: verify no ESPNow source files compile and no extra ESPNow-specific symbols are present in the map file.
- `RAFT_SYSMODS_ENABLE_ESPNOW` ON: verify compile with WiFi enabled.
- Linux/unit builds: verify ESPNow code is excluded.

Functional tests:

- One configured peer sends a simple RICREST URL command.
- Device responds on the same peer channel.
- Peer sends dynamic subscription request and receives publish messages.
- Queue-full behavior under high publish rate.
- Peer offline behavior sets `noConn` and does not permanently block CommsChannelManager.
- Unknown peer rejection.
- Wrong channel rejection or warning.
- Optional broadcast discovery.

Stress tests:

- Burst command/response traffic.
- Publish traffic while command responses are pending.
- Fragmented messages larger than one ESPNow datagram.
- WiFi STA connected to AP versus fixed AP/channel mode.
- BLE + ESPNow coexistence if both are selected.

## Resolved Design Decisions

1. ESPNow should require `NetMan` for the first implementation.
2. Configured peers should come first, with dynamic peer learning added in a later stage using a reserved channel/peer slot model.
3. Unencrypted ESPNow is acceptable for the first development pass.
4. File/stream operation is an eventual requirement. The initial implementation should not preclude it, but production-quality streaming should wait for ACK/retry and stream-specific testing.
5. Broadcast discovery is desirable but can come after basic configured peers and dynamic peer learning.
6. ESPNow support can target ESP-IDF 6.0+ and Raft-to-Raft peers. Larger ESP-NOW v2 payloads can be used later when capability is known, but the first implementation should default to 250-byte payloads.

## Key Risks

- WiFi channel mismatch when STA mode connects to an AP on a different channel than peers.
- Callback context misuse if ESPNow receive/send callbacks call heavy Raft code directly.
- Packet loss causing HDLC frames to be dropped without retry.
- Binary-size creep if RaftSysMods keeps unconditional source/dependency inclusion.
- Static publish configuration may need additional StatePublisher work if required at boot.
- ESPNow encryption configuration may fail at runtime if sdkconfig encryption peer slots are not set high enough.

## Recommended First Milestone Definition

The smallest useful milestone is:

- ESPNowManager in RaftSysMods behind `RAFT_SYSMODS_ENABLE_ESPNOW`.
- `NetMan` dependency for WiFi ownership.
- Configured unencrypted unicast peer list.
- One CommsCore channel per peer.
- `RICSerial` over fragmented ESPNow datagrams using a conservative 250-byte payload cap by default.
- Command/response through ProtocolExchange.
- Dynamic subscription/publish over ESPNow.
- Status/debug endpoints and counters.
- No RaftCore source changes.

This gives a working transport that behaves like BLE/WebSocket/Serial from ProtocolExchange's point of view while keeping ESPNow optional and outside the main RaftCore library.
