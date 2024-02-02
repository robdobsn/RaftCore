# RaftCore

Raft is a framework for ESP32 development which comprises:
- Configuration system using JSON config files and overridable options
- Communications chanels supporting BLE, WiFi & WebSockets and USB serial with consistent messaging protocols
- WebServer with support for static files, REST API and websockets
- I2C polling and device management model
- Flexible publishing mechanism for high speed outbound data comms
- REST API for imperative commands
- Audio streaming

Supported devices:
- ESP32
- ESP32 S3
- ESP32 C3

Supported frameworks:
- ESP IDF
- Arduino

This is the Core component of Raft which provides low-level functionality

Provides the following:
- Handling of SysMods (system-modules)
- RaftJson (parameter extraction from JSON documents)
- JSON configuration of SysMods
- Communications system which unifies REST API, BLE and serial comms
- Extensible logging functionality
- Timeout handing (isTimeout(), timeToTimeout(), etc)
- ESP32 specifics (enableCore0WDT(), getSystemMACAddressStr(), utilsGetSPIRAMSize(), etc)

For ESP IDF based projects the following are provided:
- Arduino time equivalents (millis(), micros(), etc)
- Arduino GPIO equivalents (pinMode(), digitialWrite(), etc)
- Arduino String (WString)

# Todo

[] improve build process
[] - WebUI generation creates files in systypes/Common/WebUI - e.g. dist, .parcel-cache, node_modules - is this ok or would it be better to specify another folder
[] - build_raft_artefacts is common to all systypes - this is by design BUT when switching from one systype to another this folder needs to be wiped
[] - changes to WebUI source code don't trigger build
[] - changes to SysType.json don't trigger build
[] raft program could handle new template
[] raft program could handle building
[] raft program could handle serial monitoring - possibly with chart??
[] raft program could handle monitoring over WiFi - maybe a stretch too far?

[] when ethernet is enabled but there is no ethernet hardware there is a long delay (5s?) on boot
[] MQTT shows >500ms when booting in the situation where eth is enabled but no hardware and maybe other times?

