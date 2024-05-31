# Todo

[] build system could maybe do more in python so it might be workable with Arduino, PlatformIO, etc
[] build system could parse sdkconfig.defaults and change the path to partitions.csv file to the right one for the systype
[] Sort out why ROS throughput is wrong
[] default hostname differs from default BLE name - BLE name has _
[] add API to set time
[] WebUI generation creates files in systypes/Common/WebUI - e.g. dist, .parcel-cache, node_modules - is this ok or would it be better to specify another folder
[] raftcli program could handle monitoring over WiFi - maybe a stretch too far?
[] when ethernet is enabled but there is no ethernet hardware there is a long delay (5s?) on boot
[] wifi password less than 8 --- what does this mean?
[] maybe implement DeviceSystem which has a device factory and basically does what HWElemManager does - perhaps change HWElemBase to RaftDevice? - basically make it so that devices can be registered from anywhere and dynamically appear from RaftI2C and will be handled by DeviceSystem so that at any time connected devices can be queried through a consistent interface - any API stuff will happen in a SysMod called DeviceManager maybe in RaftSysMods like the relationship between networkSystem and networkManager - perhaps deviceSystem is a singleton like networkSystem? - and devices can be configured from main config through DeviceManager

[] build with web server fails sometimes due to output names changing - need a different way to determine destination output - maybe a dummy file?


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

