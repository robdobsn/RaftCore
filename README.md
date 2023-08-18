# RaftCore

Raft is a framework for ESP32 development which comprises:
- Configuration system using JSON config files and overridable options
- Communications chanels supporting BLE, WiFi & WebSockets and USB serial with consistent messaging protocols
- WebServer
- I2C polling and device management model
- Flexible publishing mechanism for high speed outbound data comms
- REST API for imperative commands
- Audio streaming

Supported devices:
- ESP32
- ESP32 S3

Supported frameworks:
- ESP IDF
- Arduino

This is the Core component of Raft which provides low-level functionality

Provides the following:
- Timeout handing (isTimeout(), timeToTimeout(), etc)
- ESP32 specifics (enableCore0WDT(), getSystemMACAddressStr(), utilsGetSPIRAMSize(), etc)
- Execution timer (ExecTimer)
- ThreadSafeQueue
- Extensible Logger
- JSON utilities based on JSMN

For ESP IDF based projects the following are provided:
- Arduino time equivalents (millis(), micros(), etc)
- Arduino GPIO equivalents (pinMode(), digitialWrite(), etc)
- Arduino String (WString)

# Todo

- [ ] Sort out why ROS throughput is wrong
- [ ] default hostname differs from default BLE name - BLE name has _
- [ ] add API to set time


