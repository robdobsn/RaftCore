import sys
import os
from pathlib import Path

def write_sentinel_systype(sentinel_systype_path, current_systype):
    with open(sentinel_systype_path, 'w') as file:
        file.write(current_systype)

def main(current_systype, sentinel_systype_path, artifacts_folder, sdkconfig_path, sdkconfig_defaults_path):

    try:
        with open(sentinel_systype_path, 'r') as file:
            sentinel_systype = file.read().strip()
    except FileNotFoundError:
        print(f"Systype file not found at {sentinel_systype_path}. Assuming first build.")
        write_sentinel_systype(sentinel_systype_path, current_systype)
        return

    # Check if the current systype is different from the sentinel systype 
    if current_systype != sentinel_systype:
        print(f"Systype changed from {sentinel_systype} to {current_systype}. Cleaning artifacts folder.")
        for item in Path(artifacts_folder).glob("*"):
            if item.is_dir():
                os.rmdir(item)
            else:
                item.unlink()
        write_sentinel_systype(sentinel_systype_path, current_systype)
        return

    # Check if the sdkconfig file in the artifacts folder is the older than
    # the sdkconfig.defaults file in the current_systype folder
    try:
        sdkconfig_time = os.path.getmtime(sdkconfig_path)
    except FileNotFoundError:
        print(f"sdkconfig file {sdkconfig_path} not found. Assuming first build.")
        return
    
    try:
        sdkconfig_defaults_time = os.path.getmtime(sdkconfig_defaults_path)
    except FileNotFoundError:
        print(f"sdkconfig.defaults file {sdkconfig_defaults_path} not found.")
        return
    
    if sdkconfig_time < sdkconfig_defaults_time:
        print(f"sdkconfig file is older than sdkconfig.defaults file. Cleaning artifacts folder.")
        for item in Path(artifacts_folder).glob("*"):
            if item.is_dir():
                os.rmdir(item)
            else:
                item.unlink()
    else:
        print("No cleaning required.")

    # Update the sentinel systype file
    write_sentinel_systype(sentinel_systype_path, current_systype)

if __name__ == "__main__":
    main(*sys.argv[1:])
