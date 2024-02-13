# RaftCore

Raft is an opinionated operating environment for the Espressif ESP32 family

Raft currently supports: ESP32, ESP32S3 and ESP32C3

Raft:

* Is a framework to build both simple and complex applications with relative ease
* Has modules (called System Modules or SysMods) for many common app requirements like:
    * managing WiFi and BLE networking
    * web-server for files and REST APIs
    * web-socket and MQTT communication
* I2C bus management
* Uses JSON to configure SysMods and hardware interfaces
* Provides consistent RPC and publish/subscribe communications over WiFi (websocket and REST API), BLE and serial interfaces
* Builds on the Arduino setup() and loop() programming convention in a more capable and consistent framework
* Re-uses and builds upon well known technologies like JSON, REST, MQTT, WebServer, REST and I2C

Supported frameworks: ESP IDF, Arduino

This is the core component of Raft which provides the base functionality of the raft operating environment

