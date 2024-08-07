/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// JSON test document large
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

static const char* JSON_test_data_large = 
        R"([)"
            R"({)"
                R"("SysType": "Test2R2",)"
                R"("CmdsAtStart": "",)"
                R"("WebUI": "",)"
                R"("SysManager": {)"
                    R"("monitorPeriodMs":10000,)"
                    R"("reportList":["NetMan","BLEMan","RobotCtrl","SysMan","ProtExchg","StatsCB"],)"
                    R"("pauseWiFiforBLE":1,)"
                    R"("slowSysModMs": 100)"
                R"(},)"
                R"("ProtExchg": {)"
                    R"("RICSerial":{)"
                        R"("FrameBound":"0xE7",)"
                        R"("CtrlEscape":"0xD7")"
                    R"(})"
                R"(},)"
                R"("NetMan": {)"
                    R"("wifiSTAEn": 1,)"
                    R"("wifiAPEn": 0,)"
                    R"("ethEn": 0,)"
                    R"("wifiSSID": "",)"
                    R"("wifiPW": "",)"
                    R"("wifiSTAScanThreshold": "OPEN",)"
                    R"("wifiAPSSID": "",)"
                    R"("wifiAPPW": "",)"
                    R"("wifiAPChannel": 1,)"
                    R"("wifiAPMaxConn": 4,)"
                    R"("wifiAPAuthMode": "WPA2_PSK",)"
                    R"("logLevel": "D",)"
                    R"("NTPServer": "pool.ntp.org",)"
                    R"("timezone": "UTC")"
                R"(},)"
                R"("ESPOTAUpdate": {)"
                    R"("OTADirect": 1)"
                R"(},)"
                R"("BLEMan": {)"
                    R"("enable": 1,)"
                    R"("outQSize": 15,)"
                    R"("sendUseInd": 1,)"
                    R"("logLevel": "D",)"
                    R"("nimLogLev": "E")"
                R"(},)"
                R"("WebServer": {)"
                    R"("enable": 1,)"
                    R"("webServerPort": 80,)"
                    R"("stdRespHeaders": [)"
                        R"("Access-Control-Allow-Origin: *")"
                    R"(],)"
                    R"("apiPrefix": "api/",)"
                    R"("fileServer": [)"
                        R"({)"
                            R"("__hwRevs__": [1],)"
                            R"("__value__": 0)"
                        R"(},)"
                        R"({)"
                            R"("__hwRevs__": [2,4,5],)"
                            R"("__value__": 1)"
                        R"(})"
                    R"(],)"
                    R"("numConnSlots": [)"
                        R"({)"
                            R"("__hwRevs__": [1],)"
                            R"("__value__": 2)"
                        R"(},)"
                        R"({)"
                            R"("__hwRevs__": [2,4,5],)"
                            R"("__value__": 12)"
                        R"(})"
                    R"(],)"
                    R"("websockets": [)"
                        R"({)"
                            R"("pfix": "ws",)"
                            R"("pcol": "RICSerial",)"
                            R"("maxConn": [)"
                                R"({)"
                                    R"("__hwRevs__": [1],)"
                                    R"("__value__": 1)"
                                R"(},)"
                                R"({)"
                                    R"("__hwRevs__": [2,4,5],)"
                                    R"("__value__": 4)"
                                R"(})"
                            R"(],)"
                            R"("txQueueMax": [)"
                                R"({)"
                                    R"("__hwRevs__": [1],)"
                                    R"("__value__": 6)"
                                R"(},)"
                                R"({)"
                                    R"("__hwRevs__": [2,4,5],)"
                                    R"("__value__": 20)"
                                R"(})"
                            R"(],)"
                            R"("pingMs": 30000)"
                        R"(})"
                    R"(],)"
                    R"("logLevel": "D",)"
                    R"("sendMax": [)"
                                R"({)"
                                    R"("__hwRevs__": [1],)"
                                    R"("__value__": 500)"
                                R"(},)"
                                R"({)"
                                    R"("__hwRevs__": [2,4,5],)"
                                    R"("__value__": 5000)"
                                R"(})"
                            R"(],)"
                    R"("taskCore": 0,)"
                    R"("taskStack": 5000,)"
                    R"("taskPriority": 9)"
                R"(},)"
                R"("SerialConsole": {)"
                    R"("enable": 1,)"
                    R"("uartNum": 0,)"
                    R"("rxBuf": 5000,)"
                    R"("txBuf": 1500,)"
                    R"("crlfOnTx": 1,)"
                    R"("protocol": "RICSerial",)"
                    R"("logLevel": "D")"
                R"(},)"
                R"("CommandSerial": {)"
                    R"("logLevel": "D",)"
                    R"("ports": [)"
                        R"({)"
                            R"("name": "Serial1",)"
                            R"("enable": 1,)"
                            R"("uartNum": 1,)"
                            R"("baudRate": 921600,)"
                            R"("rxBufSize": 8192,)"
                            R"("rxPin": 35,)"
                            R"("txPin": 12,)"
                            R"("protocol": "RICSerial")"
                        R"(})"
                    R"(])"
                R"(},)"
                R"("CommandSocket": {)"
                    R"("enable": 0,)"
                    R"("socketPort": 24,)"
                    R"("protocol": "RICSerial",)"
                    R"("logLevel": "D")"
                R"(},)"
                R"("FileManager": {)"
                    R"("SPIFFSEnabled": 1,)"
                    R"("LittleFSEnabled": 1,)"
                    R"("LocalFSFormatIfCorrupt": 1,)"
                    R"("CacheFileSysInfo": 1,)"
                    R"("SDEnabled": 0,)"
                    R"("DefaultSD": 0,)"
                    R"("SDMOSI": 15,)"
                    R"("SDMISO": 4,)"
                    R"("SDCLK": 14,)"
                    R"("SDCS": 13)"
                R"(},)"
                R"("PowerUpLED":{)"
                    R"("enable":0,)"
                    R"("ledPin":19,)"
                    R"("ledVal":"200000",)"
                    R"("isPix":1,)"
                    R"("pixIdx":0,)"
                    R"("usbSnsPin": [{"__hwRevs__": [2,4,5], "__value__": 39}],)"
                    R"("usbSnsThresh": 500,)"
                    R"("usbSnsVal": "000040")"
                R"(},)"
                R"("uPy": {)"
                    R"("maxHeapNoSPIRAM": 14000,)"
                    R"("taskStackNoSPIRAM": 12000,)"
                    R"("maxHeapPCIfSPIRAM": 80,)"
                    R"("taskStackIfSPIRAM": 16000,)"
                    R"("taskPriority": 20)"
                R"(},)"
                R"("Publish": {)"
                    R"("pubList": [)"
                        R"({)"
                            R"("type": "HW",)"
                            R"("name": "MultiStatus",)"
                            R"("trigger": "Time",)"
                            R"("msgID": "MultiStatus",)"
                            R"("rates": [)"
                                R"({)"
                                    R"("if": "BLE",)"
                                    R"("protocol": "RICSerial",)"
                                    R"("rateHz": 10.0)"
                                R"(},)"
                                R"({)"
                                    R"("if": "uPy",)"
                                    R"("protocol": "RICFrame",)"
                                    R"("rateHz": 10.0)"
                                R"(})"
                            R"(])"
                        R"(},)"
                        R"({)"
                            R"("type": "HW",)"
                            R"("name": "PowerStatus",)"
                            R"("trigger": "Time",)"
                            R"("msgID": "PowerStatus",)"
                            R"("rates": [)"
                                R"({)"
                                    R"("if": "BLE",)"
                                    R"("protocol": "RICSerial",)"
                                    R"("rateHz": 1.0)"
                                R"(},)"
                                R"({)"
                                    R"("if": "uPy",)"
                                    R"("protocol": "RICFrame",)"
                                    R"("rateHz": 1.0)"
                                R"(})"
                            R"(])"
                        R"(},)"
                        R"({)"
                            R"("type": "HW",)"
                            R"("name": "AddOnStatus",)"
                            R"("trigger": "Time",)"
                            R"("msgID": "AddOnStatus",)"
                            R"("rates": [)"
                                R"({)"
                                    R"("if": "BLE",)"
                                    R"("protocol": "RICSerial",)"
                                    R"("rateHz": 10.0)"
                                R"(},)"
                                R"({)"
                                    R"("if": "uPy",)"
                                    R"("protocol": "RICFrame",)"
                                    R"("rateHz": 10.0)"
                                R"(})"
                            R"(])"
                        R"(})"
                    R"(])"
                R"(},)"
                R"("Robot": {)"
                    R"("Model": "TestV2",)"
                    R"("StatDisp": {)"
                        R"("pixIdxPower": 0,)"
                        R"("pixIdxConn": 2)"
                    R"(},)"
                    R"("Safeties": {)"
                        R"("maxMs": 5000,)"
                        R"("ffEn": 1,)"
                        R"("ocEn": 1,)"
                        R"("i2cEn": 1,)"
                        R"("sfEn": 1)"
                    R"(},)"
                    R"("PwrMgmt": {)"
                        R"("initMotorsOn": 1,)"
                        R"("idle5vOffSecs": 1800,)"
                        R"("idlePowerDownSecs": 2700,)"
                        R"("battFullCapMAH": 2500,)"
                        R"("batLevels": [)"
                            R"({)"
                                R"("PC": 0,)"
                                R"("5VOn": 1,)"
                                R"("colr": "002000",)"
                                R"("blnk": "on")"
                            R"(},)"
                            R"({)"
                                R"("PC": 3,)"
                                R"("5VOn": 0,)"
                                R"("colr": "100000",)"
                                R"("blnk": "breathe",)"
                                R"("fullOff": 1)"
                            R"(},)"
                            R"({)"
                                R"("PC": 5,)"
                                R"("5VOn": 0,)"
                                R"("colr": "100000",)"
                                R"("blnk": "breathe")"
                            R"(},)"
                            R"({)"
                                R"("PC": 10,)"
                                R"("5VOn": 1,)"
                                R"("colr": "100000",)"
                                R"("blnk": "on")"
                            R"(},)"
                            R"({)"
                                R"("PC": 30,)"
                                R"("5VOn": 1,)"
                                R"("colr": "100400",)"
                                R"("blnk": "on")"
                            R"(},)"
                            R"({)"
                                R"("PC": 100,)"
                                R"("5VOn": 1,)"
                                R"("colr": "002000",)"
                                R"("blnk": "on")"
                            R"(})"
                        R"(],)"
                        R"("extLevels": [)"
                            R"({)"
                                R"("PC": 0,)"
                                R"("colr": "000040",)"
                                R"("blnk": "on")"
                            R"(},)"
                            R"({)"
                                R"("PC": 90,)"
                                R"("colr": "000040",)"
                                R"("blnk": "breathe")"
                            R"(},)"
                            R"({)"
                                R"("PC": 100,)"
                                R"("colr": "000040",)"
                                R"("blnk": "on")"
                            R"(})"
                        R"(])"
                    R"(},)"
                    R"("Events": {)"
                        R"("det": [)"
                            R"({)"
                                R"("src": "RicButton0",)"
                                R"("type": "itermenu",)"
                                R"("cancelMs": 100,)"
                                R"("activeMs": 200,)"
                                R"("cancelSound": "no.mp3",)"
                                R"("itermenu": [)"
                                    R"({)"
                                        R"("sound": "unplugged.mp3",)"
                                        R"("colr": "101010",)"
                                        R"("url": "filerun/unplugged.api",)"
                                        R"("stop": "filerun/unpluggedShutdown.api")"
                                    R"(})"
                                R"(])"
                            R"(})"
                        R"(])"
                    R"(},)"
                    R"("Buses": [)"
                        R"({)"
                            R"("name": "I2CA",)"
                            R"("type": "I2C",)"
                            R"("reqFIFOLen": 10,)"
                            R"("i2cPort": 0,)"
                            R"("sdaPin": "21",)"
                            R"("sclPin": "22",)"
                            R"("i2cFreq": 100000,)"
                            R"("taskCore": 1,)"
                            R"("taskStack": 3000,)"
                            R"("taskPriority": 24,)"
                            R"("lockupDetect": [)"
                                R"({"__hwRevs__": [1,2], "__value__": "0x1d"},)"
                                R"({"__hwRevs__": [4, 5], "__value__": "0x0e"})"
                            R"(],)"
                            R"("scanBoost": [)"
                                R"({"__hwRevs__": [1,2], "__value__": ["0x1d","0x55"]},)"
                                R"({"__hwRevs__": [4, 5], "__value__": ["0x0e","0x55"]})"
                            R"(])"
                        R"(},)"
                        R"([)"
                            R"({)"
                                R"("__hwRevs__": [1],)"
                                R"("__value__": {)"
                                    R"("name": "I2CB",)"
                                    R"("type": "I2C",)"
                                    R"("reqFIFOLen": 10,)"
                                    R"("i2cPort": 1,)"
                                    R"("sdaPin": "16",)"
                                    R"("sclPin": "17",)"
                                    R"("i2cFreq": 100000,)"
                                    R"("taskCore": 1,)"
                                    R"("taskStack": 3000,)"
                                    R"("taskPriority": 24,)"
                                    R"("lowLoad": 1)"
                                R"(})"
                            R"(})"
                        R"(])"
                    R"(],)"
                    R"("HWElemDefaults": [)"
                        R"({)"
                            R"("type": "SmartServo",)"
                            R"("bus": "I2CA",)"
                            R"("poll": "status",)"
                            R"("pollHz": 20.0,)"
                            R"("ocSafety": 1,)"
                            R"("faultSafety": 1)"
                        R"(},)"
                        R"({)"
                            R"("type": "RSAddOn",)"
                            R"("poll": "status",)"
                            R"("pollRd": 10,)"
                            R"("pollHz": 10.0)"
                        R"(})"
                    R"(],)"
                    R"("HWElems": [)"
                        R"({)"
                            R"("name": "LeftHip",)"
                            R"("type": "SmartServo",)"
                            R"("addr": "0x10",)"
                            R"("IDNo": 0)"
                        R"(},)"
                        R"({)"
                            R"("name": "LeftTwist",)"
                            R"("type": "SmartServo",)"
                            R"("addr": "0x11",)"
                            R"("IDNo": 1)"
                        R"(},)"
                        R"({)"
                            R"("name": "LeftKnee",)"
                            R"("type": "SmartServo",)"
                            R"("addr": "0x12",)"
                            R"("IDNo": 2)"
                        R"(},)"
                        R"({)"
                            R"("name": "RightHip",)"
                            R"("type": "SmartServo",)"
                            R"("addr": "0x13",)"
                            R"("IDNo": 3)"
                        R"(},)"
                        R"({)"
                            R"("name": "RightTwist",)"
                            R"("type": "SmartServo",)"
                            R"("addr": "0x14",)"
                            R"("IDNo": 4)"
                        R"(},)"
                        R"({)"
                            R"("name": "RightKnee",)"
                            R"("type": "SmartServo",)"
                            R"("addr": "0x15",)"
                            R"("IDNo": 5)"
                        R"(},)"
                        R"({)"
                            R"("name": "LeftArm",)"
                            R"("type": "SmartServo",)"
                            R"("addr": "0x16",)"
                            R"("IDNo": 6)"
                        R"(},)"
                        R"({)"
                            R"("name": "RightArm",)"
                            R"("type": "SmartServo",)"
                            R"("addr": "0x17",)"
                            R"("IDNo": 7)"
                        R"(},)"
                        R"({)"
                            R"("name": "Eyes",)"
                            R"("type": "SmartServo",)"
                            R"("addr": "0x18",)"
                            R"("IDNo": 8)"
                        R"(},)"
                        R"({)"
                            R"("name": "IMU0",)"
                            R"("type": "IMU",)"
                            R"("addr": [)"
                                R"({"__hwRevs__": [1,2], "__value__": "0x1d"},)"
                                R"({"__hwRevs__": [4,5], "__value__":  "0x0e"})"
                            R"(],)"
                            R"("chip": [)"
                                R"({"__hwRevs__": [1,2], "__value__": "MMA8452Q"},)"
                                R"({"__hwRevs__": [4,5], "__value__": "KXTJ3"})"
                            R"(],)"
                            R"("bus": "I2CA",)"
                            R"("poll": "status",)"
                            R"("pollHz": 20.0,)"
                            R"("accIntPin": [)"
                                R"({"__hwRevs__": [1,2], "__value__": 34},)"
                                R"({"__hwRevs__": [4,5], "__value__": -1})"
                            R"(],)"
                            R"("IDNo": 19)"
                        R"(},)"
                        R"({)"
                            R"("name": "AudioOut",)"
                            R"("type": "AudioOut",)"
                            R"("I2S_PORT": 0,)"
                            R"("DAC_MODE": [)"
                                R"({"__hwRevs__": [1,2,4], "__value__": 0},)"
                                R"({"__hwRevs__": [5], "__value__": 1})"
                            R"(],)"
                            R"("REV1_AMP_EN": [{"__hwRevs__": [1], "__value__": 18}],)"
                            R"("AMP_EN": [{"__hwRevs__": [5], "__value__": 27}],)"
                            R"("I2S_BLCK": [)"
                                R"({"__hwRevs__": [1,2,4], "__value__": 26},)"
                                R"({"__hwRevs__": [5], "__value__": -1})"
                            R"(],)"
                            R"("I2S_DOUT": [)"
                                R"({"__hwRevs__": [1,2,4], "__value__": 25},)"
                                R"({"__hwRevs__": [5], "__value__": -1})"
                            R"(],)"
                            R"("I2S_LRC": [)"
                                R"({"__hwRevs__": [1,2,4], "__value__": 27},)"
                                R"({"__hwRevs__": [5], "__value__": -1})"
                            R"(],)"
                            R"("sampleRate": 44100,)"
                            R"("minIntRAM": 25000)"
                        R"(},)"
                        R"({)"
                            R"("name": "BusPixels0",)"
                            R"("type": "BusPixels",)"
                            R"("pixPin": "19",)"
                            R"("rmtChannel": 0,)"
                            R"("numPix": [)"
                                R"({)"
                                    R"("__hwRevs__": [1],)"
                                    R"("__value__": 3)"
                                R"(},)"
                                R"({)"
                                    R"("__hwRevs__": [2,4,5],)"
                                    R"("__value__": 17)"
                                R"(})"
                            R"(],)"
                            R"("statusPix": [)"
                                R"({)"
                                    R"("__hwRevs__": [1],)"
                                    R"("__value__": [0, 1, 2])"
                                R"(},)"
                                R"({)"
                                    R"("__hwRevs__": [2,4,5],)"
                                    R"("__value__": [0, 1, 2, 3, 4])"
                                R"(})"
                            R"(])"
                        R"(},)"
                        R"({)"
                            R"("name": "LeftLEDeye",)"
                            R"("whoami": "LEDeye",)"
                            R"("type": "BusPixels",)"
                            R"("pixPin": "26",)"
                            R"("rmtChannel": 1,)"
                            R"("numPix": [{"__hwRevs__": [5], "__value__": 12}])"
                        R"(},)"
                        R"({)"
                            R"("name": "RightLEDeye",)"
                            R"("whoami": "LEDeye",)"
                            R"("type": "BusPixels",)"
                            R"("subsetOf": "BusPixels0",)"
                            R"("pixOffset": 5,)"
                            R"("numPix": [{"__hwRevs__": [5], "__value__": 12}])"
                        R"(},)"
                        R"({)"
                            R"("name": "RicButton0",)"
                            R"("type": "GPIO",)"
                            R"("pin": "5",)"
                            R"("opMode": "button",)"
                            R"("pull": "up",)"
                            R"("debounceMs": "100",)"
                            R"("repeatMs": "10000")"
                        R"(},)"
                        R"({)"
                            R"("name": "FuelGauge0",)"
                            R"("type": "FuelGauge",)"
                            R"("bus": "I2CA",)"
                            R"("addr": "0x55",)"
                            R"("poll": "status",)"
                            R"("pollHz": 1.0,)"
                            R"("extPowSnsPin": [{"__hwRevs__": [2,4,5], "__value__": 39}],)"
                            R"("extPowSnsThresh": 500)"
                        R"(},)"
                        R"({)"
                            R"("name": "PowerCtrl",)"
                            R"("type": "PowerCtrl",)"
                            R"("batteryPackKey": 23,)"
                            R"("en5V": 2,)"
                            R"("en5VOnLevel": 1,)"
                            R"("en5VUsePWM": [{"__hwRevs__": [1,2,5], "__value__": 1}],)"
                            R"("enExtPowerUsePWM": [{"__hwRevs__": [4], "__value__": 1}],)"
                            R"("extPowerInitState": [{"__hwRevs__": [4], "__value__": 1}],)"
                            R"("pwrKeyOffSecs": 0.7,)"
                            R"("CHG_EN": [{"__hwRevs__": [2], "__value__": 18}],)"
                            R"("EXT_POWER": [{"__hwRevs__": [4,5], "__value__": 18}])"
                        R"(})"
                    R"(],)"
                    R"("AddOns": [],)"
                    R"("Traj": {)"
                        R"("init": {)"
                            R"("moveTimeMin": 400,)"
                            R"("hipAngleMax": 30,)"
                            R"("stepLengthMax": 50,)"
                            R"("leanMultMax": 1,)"
                            R"("DIR_FORWARDS": 1,)"
                            R"("DIR_BACKWARDS": -1,)"
                            R"("DIR_LEFT": -1,)"
                            R"("DIR_RIGHT": 1,)"
                            R"("moveTime": 1500,)"
                            R"("leanAmount": 27,)"
                            R"("hipPosEqThreshold": 8.5,)"
                            R"("stepLength": 15,)"
                            R"("turn": 0,)"
                            R"("leanAngle": 35,)"
                            R"("liftAngle": 65)"
                        R"(},)"
                        R"("hwVars": [],)"
                        R"("maxKeyPt": 20,)"
                        R"("maxTrajReps": 100,)"
                        R"("maxProcQLen": 10)"
                    R"(},)"
                    R"("WorkMgr": {)"
                        R"("WorkQ": {)"
                            R"("maxLen": [)"
                                R"({)"
                                    R"("__hwRevs__": [1],)"
                                    R"("__value__": 50)"
                                R"(},)"
                                R"({)"
                                    R"("__hwRevs__": [2,4,5],)"
                                    R"("__value__": 250)"
                                R"(})"
                            R"(],)"
                            R"("lowMemThreshK": 15,)"
                            R"("lowMemMaxLen": 10)"
                        R"(})"
                    R"(})"
                R"(})"
            R"(})"
        R"(])"
        ;

static const char* JSON_test_data_large_end = JSON_test_data_large + strlen(JSON_test_data_large);
