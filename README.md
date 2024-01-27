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



