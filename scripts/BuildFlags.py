import os
import datetime

# Script to generate the -I flags for the PlatformIO build environment
# Rob Dobson 2024

# Current working directory
current_dir = os.getcwd()

# Components folder relative to the scripts folder
components_folder = os.path.normpath(os.path.join(current_dir, 'components'))

# Find all folders containing .h files within the components directory
header_folders = set()
for root, dirs, files in os.walk(components_folder):
    if any(f.endswith('.h') for f in files):
        relative_path = os.path.relpath(root, os.path.join(current_dir, ''))
        # Replace backslashes with forward slashes
        relative_path = relative_path.replace('\\', '/')
        header_folders.add(relative_path)

# Generate the -I flags for each header folder
header_flags = [f'-I{f}' for f in header_folders]

# Print the flags as a space-separated string
output_flags = " ".join(header_flags)

# Debug write to file with name based on current date and time
# fname = 'output_flags_' + datetime.datetime.now().strftime("%Y%m%d%H%M%S") + '.txt'
# with open(fname, 'w') as f:
#     f.write(output_flags)

# Print the flags
print(output_flags)
