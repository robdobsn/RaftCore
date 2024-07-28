# import os
# # Current working directory
# current_dir = os.getcwd()
# # print("----- Current Dir: ", current_dir)

# # Recurse folders to find all folders containing .h files starting in the ../components folder
# components_folder = os.path.normpath(os.path.join(current_dir, '..', 'components'))

# # Find all folders containing .h files
# header_folders = set()
# for root, dirs, files in os.walk(components_folder):
#     if any([f.endswith('.h') for f in files]):
#         relative_path = os.path.relpath(root, current_dir)
#         header_folders.add(relative_path)

# # print("----- Header Folders: ", header_folders)

# # Generate the -I flags for each header folder
# header_flags = [f'-I{f}' for f in list(header_folders)]
# print(" ".join(header_flags))

# import os

# # Current working directory
# current_dir = os.getcwd()

# # Components folder relative to the scripts folder
# components_folder = os.path.normpath(os.path.join(current_dir, '..', 'components'))

# # Find all folders containing .h files within the components directory
# header_folders = set()
# for root, dirs, files in os.walk(components_folder):
#     if any(f.endswith('.h') for f in files):
#         relative_path = os.path.relpath(root, os.path.join(current_dir, '..'))
#         # Replace backslashes with forward slashes
#         relative_path = relative_path.replace('\\', '/')
#         header_folders.add(relative_path)

# # Generate the -I flags for each header folder
# header_flags = [f'-I{f}' for f in header_folders]

# # Print the flags as a space-separated string
# print(" ".join(header_flags))


# print("-Icomponents/core/Logger")

print("-Icomponents/core/ESPMDNS -Icomponents/core/DeviceManager -Icomponents/core/RingBuffer -Icomponents/core/StatusIndicator -Icomponents/core/RaftDevice -Icomponents/comms/ProtocolRICFrame -Icomponents/core/MiniHDLC -Icomponents/core/MQTT -Icomponents/core/SysManager -Icomponents/core/SysTypes -Icomponents/comms/ProtocolRICJSON -Icomponents/core/FileSystem -Icomponents/comms/FileStreamProtocols -Icomponents/comms/CommsChannelMsg -Icomponents/comms/ProtocolRawMsg -Icomponents/core/SupervisorStats -Icomponents/core/RaftJson -Icomponents/core/Bus -Icomponents/core/Logger -Icomponents/core/NetworkSystem -Icomponents/core/ExpressionEval -Icomponents/core/Utils -Icomponents/core/DeviceTypes -Icomponents/core/ArPreferences -Icomponents/core/libb64 -Icomponents/core/APICommon -Icomponents/core/LEDPixels -Icomponents/comms/RICRESTMsg -Icomponents/core/SysMod -Icomponents/comms/CommsChannels -Icomponents/comms/ProtocolBase -Icomponents/comms/CommsBridgeMsg -Icomponents/core/NumericalFilters -Icomponents/core/NamedValueProvider -Icomponents/comms/ProtocolOverAscii -Icomponents/comms/ProtocolExchange -Icomponents/core/ThreadSafeQueue -Icomponents/comms/ProtocolRICSerial -Icomponents/core/DNSResolver -Icomponents/core/DebounceButton -Icomponents/core/ConfigPinMap -Icomponents/comms/CommsCoreIF -Icomponents/core/RestAPIEndpoints -Icomponents/comms/ROSSerial -Icomponents/core/ArduinoUtils -Icomponents/core/RaftCoreApp -Icomponents/core/DebugGlobals")
