# Todo

[] implement a mechanism that allows coord system transforms like rotation/inversion/etc - either as an expression or as a named transformation
[] provide a way to set the sampling rate for devices - similar to actions - there may be a need to send an I2C command to device to change internal rate - and also to change the reporting rate / publish rate?
[] dockerfile and other build specific stuff - maybe allow different ESP IDF versions???
[] move RaftCore version into the sysmod somehow - need to think how CMake works in this case
[] move RaftCore into build_raft_artifacts or otherwise adjust -> raft_build
[] consider compressing device types json
[] add DeviceFactory.h to RaftCoreApp.h - consider name change to RaftDeviceFactory
[] change poll_XXX to raft_XXX for raft device devode
[] debug msg in sample collector long     LOG_I(MODULE_PREFIX, "setup sampleRateLimitHz %d maxTotalJSONStringSize %d sampleHeader %s sampleAPIName %s allocateAtStart %s dumpToConsole %d dumpToFileName %s maxFileSize %d",
[] build system could maybe do more in python so it might be workable with Arduino, PlatformIO, etc
[] build system could parse sdkconfig.defaults and change the path to partitions.csv file to the right one for the systype
[] Sort out why ROS throughput is wrong
[] default hostname differs from default BLE name - BLE name has _
[] add API to set time
[] WebUI generation creates files in systypes/Common/WebUI - e.g. dist, .parcel-cache, node_modules - is this ok or would it be better to specify another folder
[] raftcli program could handle monitoring over WiFi - maybe a stretch too far?
[] when ethernet is enabled but there is no ethernet hardware there is a long delay (5s?) on boot
[] wifi password less than 8 --- what does this mean?

build script thoughts:
- a python script to do the pre-build
- consistent way of defining Raft libs and git tags
- process web ui properly
- handle systype
-- if no systype specified then look for the last one built? if nothing built then find the first one? if no systype folder then what?
- does running idf.py with -b build/<systype> work?
- how does it work for arduino IDE
- and for platformio
- what about sdkconfig.defaults
- what about partitions.csv which is named in sdkconfig? plaformio has its own way of handling this
- platform io boards, etc

questions:
- how does the python script get into place?
-- if it is part of RaftCore then what loads RaftCore?
-- maybe it is part of RaftCLI but then what if using Ardunino IDE or PlatformIO?

