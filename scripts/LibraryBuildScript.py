import os
import subprocess
from SCons.Script import DefaultEnvironment

def is_pio_build():
    from SCons.Script import DefaultEnvironment
    env = DefaultEnvironment()
    
print("----------------- Running LibraryBuildScript.py -----------------")

env = DefaultEnvironment()

print(env.Dump())

print("----------------- Generating Device Records -----------------")
# Define paths
current_dir = os.path.dirname(os.path.abspath(__file__))
print("----- Current Dir: ", current_dir)
json_file = os.path.join(current_dir, 'devtypes', 'DeviceTypeRecords.json')
print("----- JSON File: ", json_file)
artifacts_folder = os.path.join(env['PROJECT_BUILD_DIR'], 'build_raft_artifacts')
print("----- Artifacts Folder: ", artifacts_folder)
dev_type_recs_header = os.path.join(artifacts_folder, 'DeviceTypeRecords_generated.h')
print("----- Device Type Records Header: ", dev_type_recs_header)
dev_poll_recs_header = os.path.join(artifacts_folder, 'DevicePollRecords_generated.h')
print("----- Device Poll Records Header: ", dev_poll_recs_header)

# Create artifacts folder if it doesn't exist
if not os.path.exists(artifacts_folder):
    os.makedirs(artifacts_folder)

# Command to generate device records headers
command = [
    env['PYTHONEXE'], os.path.join(current_dir, 'scripts', 'ProcessDevTypeJsonToC.py'),
    json_file, dev_type_recs_header, dev_poll_recs_header
]

print("----- Command: ", command)

# Run the command
result = subprocess.run(command, capture_output=True, text=True)
if result.returncode != 0:
    print(f"Error generating device records: {result.stderr}")
    env.Exit(result.returncode)
else:
    print("Generated device records successfully")
