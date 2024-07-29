import os

# Script to generate defines from a kconfig file for use in PlatformIO build environment
# Rob Dobson 2024

def process_kconfig_file(kconfig_file):
    defines = {}
    with open(kconfig_file, 'r') as file:
        lines = file.readlines()
    
    current_key = None
    for line in lines:
        line = line.strip()
        if line.startswith("config"):
            current_key = line.split()[1]
            defines[current_key] = None
        elif line.startswith("default") and current_key:
            default_value = line.split()[1]
            if default_value.isdigit():
                defines[current_key] = int(default_value)
            elif default_value in ["y", "n"]:
                defines[current_key] = 1 if default_value == "y" else 0
            else:
                defines[current_key] = default_value
        elif line.startswith("help") or line.startswith("choice") or line.startswith("endchoice"):
            current_key = None

    return defines

