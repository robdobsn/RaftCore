import os
import subprocess
from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()

def generate_device_records(source, target, env):
    # Define paths
    current_dir = os.path.dirname(os.path.abspath(__file__))
    json_file = os.path.join(current_dir, 'devtypes', 'DeviceTypeRecords.json')
    artifacts_folder = os.path.join(env['PROJECT_BUILD_DIR'], 'build_raft_artifacts')
    dev_type_recs_header = os.path.join(artifacts_folder, 'DeviceTypeRecords_generated.h')
    dev_poll_recs_header = os.path.join(artifacts_folder, 'DevicePollRecords_generated.h')

    # Create artifacts folder if it doesn't exist
    if not os.path.exists(artifacts_folder):
        os.makedirs(artifacts_folder)

    # Command to generate device records headers
    command = [
        env['PYTHONEXE'], os.path.join(current_dir, 'scripts', 'ProcessDevTypeJsonToC.py'),
        json_file, dev_type_recs_header, dev_poll_recs_header
    ]

    # Run the command
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Error generating device records: {result.stderr}")
        env.Exit(result.returncode)
    else:
        print("Generated device records successfully")

# Register the custom builder
env.AddPreAction("buildprog", generate_device_records)
