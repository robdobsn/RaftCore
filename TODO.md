# Todo

[] Sort out why ROS throughput is wrong
[] default hostname differs from default BLE name - BLE name has _
[] add API to set time
[] improve build process
[] - WebUI generation creates files in systypes/Common/WebUI - e.g. dist, .parcel-cache, node_modules - is this ok or would it be better to specify another folder
[] - build_raft_artefacts is common to all systypes - this is by design BUT when switching from one systype to another this folder needs to be wiped
[] - changes to WebUI source code don't trigger build
[] - changes to SysType.json don't trigger build
[] raftcli could handle building
[] raftcli program could handle monitoring over WiFi - maybe a stretch too far?
[] when ethernet is enabled but there is no ethernet hardware there is a long delay (5s?) on boot
[] MQTT shows >500ms when booting in the situation where eth is enabled but no hardware and maybe other times?
