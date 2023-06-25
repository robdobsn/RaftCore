import argparse
import json
import os.path

def parseCMakeLists(cmake_file_name):

    print(f'-----------\nParsing {cmake_file_name}')
    # Process CMakeLists.txt
    with open(cmake_file_name, 'r') as f:
        cmakelists_lines = f.readlines()
    in_srcs_section = False
    in_include_section = False
    in_require_section = False
    files = []
    includes = []
    requires = []
    for line in cmakelists_lines:
        if 'SRCS' in line:
            in_srcs_section = True
            continue
        if 'INCLUDE_DIRS' in line:
            in_srcs_section = False
            in_include_section = True
            continue
        if 'REQUIRES' in line:
            in_include_section = False
            in_require_section = True
            continue
        if ")" in line:
            in_require_section = False
            continue
        if in_srcs_section:
            src_file_name = line.strip().replace('"', "")
            files.append(src_file_name)
        elif in_include_section:
            include_path = line.strip().replace('"', "")
            includes.append(include_path)
        elif in_require_section:
            requires.append(line.strip())

    # Discover the component folder
    component_folder = ''
    potential_base_component_folders = []
    if len(files) > 0:
        test_name = files[0]
        while True:
            base_path = os.path.split(test_name)[0]
            if base_path == '':
                break
            potential_base_component_folders.append(base_path)
            test_name = base_path
    for potential_base_component_folder in potential_base_component_folders:
        if all([x.startswith(potential_base_component_folder) for x in files]):
            component_folder = potential_base_component_folder
            break

    # Remove component folder from file names and include paths
    if component_folder != '':
        files = [x[len(component_folder) + 1:] for x in files]
        includes = [x[len(component_folder) + 1:] if len(x) > len(component_folder) else '.' for x in includes]

    print(f"Component folder from : {component_folder}")
    print(f'Files:', files)
    print(f'Includes:', includes)
    print(f'Requires:', requires)

    return files, includes, requires, component_folder

def createCMakeLists(cmake_file_name, comp_folder, new_sub_folders, new_files, new_root_requires):
    # Check if we should create a CMakeLists.txt
    print(f'-----------\nCreating {cmake_file_name}')
    with open(cmake_file_name, 'w') as f:
        f.write('cmake_minimum_required(VERSION 3.16)\n\nidf_component_register(\n  SRCS\n')
        new_files = sorted(new_files, key=str.casefold)
        f.writelines([f'    "{os.path.join(comp_folder, x)}"\n' for x in new_files])
        f.write('  INCLUDE_DIRS\n')
        new_sub_folders = sorted(new_sub_folders, key=str.casefold)
        f.writelines([f'    "{os.path.join(comp_folder, x)}"\n' for x in new_sub_folders])
        f.write('  REQUIRES\n')
        f.writelines([f'    {x}\n' for x in new_root_requires])
        f.write(')\n')
        
print('-----------\nChecking imports in library.json and CMakeLists.txt\n-----------')
parser = argparse.ArgumentParser(description='Compare imports in library.json and CMakeFile.txt')
parser.add_argument('-l', '--lib', type=str, help='path to library.json', default='library.json')
parser.add_argument('-c', '--cmake', type=str, help='path to CMakeLists.txt', default='CMakeLists.txt')
args = parser.parse_args()

# Extract include flags from library.json
with open(args.lib, 'r') as f:
    library_json = json.load(f)
    fixed_library_json = library_json.copy()
build_flags = library_json['build']['flags']
lib_included_folders = [x[2:] for x in build_flags if x.startswith('-I')]

# Extract sections from root CMakeLists.txt
root_files, root_includes, root_requires, component_folder = parseCMakeLists(args.cmake)

# Check that all included folders in library.json are in the same component folder
for folder in lib_included_folders:
    if not folder.startswith(component_folder):
        print(f'-----------\nlibrary.json included folders are not all in the same component folder: {folder} is not in {component_folder}')
        exit(1)

# Extract sub-folder names
lib_sub_folders = [x[len(component_folder) + 1:] if len(x) > len(component_folder) else '.' for x in lib_included_folders]
print(f'-----------\nSub-folders in library.json: {lib_sub_folders}')

# Create sets of all sub folders
all_sub_folders = set([os.path.split(x)[0] for x in root_files if len(os.path.split(x)[0]) > 0])
all_sub_folders.update([x for x in root_includes])
all_sub_folders.update(lib_sub_folders)

# Create set of all files
all_files = set(root_files)

# Check if there is a CMakelists.txt in the component folder
component_cmake_file_name = os.path.join(component_folder, 'CMakeLists.txt')
component_files = []
component_includes = []
if os.path.isfile(component_cmake_file_name):

    # Extract sections from the component CMakeLists.txt
    component_files, component_includes, _, _ = parseCMakeLists(component_cmake_file_name)

    # Add to the set of folders and files
    all_sub_folders.update([os.path.split(x)[0] for x in component_files if len(os.path.split(x)[0]) > 0])
    all_sub_folders.update([x for x in component_includes])
    all_files.update(component_files)

# Debug
print(f'-----------\nAll sub folders in all places: {all_sub_folders}')
print(f'-----------\nAll files in all places: {all_files}')

print(f'====================== CHECKS ==========================')

# Check that all sub-folders exist
folders_to_remove = []
for folder in all_sub_folders:
    full_path_folder = os.path.join(component_folder, folder)
    if not os.path.isdir(full_path_folder):
        print(f'Sub-folder {folder} does not exist')
        folders_to_remove.append(folder)
        fix_required = True
for folder in folders_to_remove:
    all_sub_folders.remove(folder)

# Check that all sub-folders are included in the JSON file
fix_required = False
for folder in all_sub_folders:
    if folder not in lib_sub_folders: 
        print(f'Sub-folder {folder} is not included in library.json')
        fix_required = True
        full_path_folder = os.path.join(component_folder, folder)
        fixed_library_json['build']['flags'].append(f'-I{full_path_folder}')

# Check that all files are included in the various CMakeLists.txt
for folder in all_sub_folders:
    if folder not in root_includes:
        print(f'File {folder} is not included in root CMakeLists.txt')
        fix_required = True
for file in all_files:
    if file not in root_files:
        print(f'File {file} is not included in root CMakeLists.txt')
        fix_required = True
    if file not in component_files:
        print(f'File {file} is not included in component CMakeLists.txt')
        fix_required = True

print(f'==================== END CHECKS ========================')

# Check that component folder has a CMakeLists.txt
component_cmake_file_name = os.path.join(component_folder, 'CMakeLists.txt')
if not os.path.isfile(component_cmake_file_name):
    # create a CMakeLists.txt in the component folder
    createCMakeLists(component_cmake_file_name, "", all_sub_folders, all_files, root_requires)
elif fix_required:
    # create a fixed_CMakeLists.txt in the component folder
    fixed_cmake_file_name = os.path.join(component_folder, 'fixed_CMakeLists.txt')
    createCMakeLists(fixed_cmake_file_name, "", all_sub_folders, all_files, root_requires)

# Check if fix required
if fix_required:
    # Sort the includes
    fixed_library_json['build']['flags'] = sorted(fixed_library_json['build']['flags'], key=str.casefold)
    # Write fixed library.json
    print(f'-----------\nWriting fixed library.json')
    fixed_library_json_file_name = os.path.join(os.path.split(args.lib)[0], "fixed_"+os.path.split(args.lib)[1])
    with open(fixed_library_json_file_name, 'w') as f:
        json.dump(fixed_library_json, f, indent=2)

    # Write fixed CMakeLists.txt in root folder
    print(f'-----------\nWriting fixed CMakeLists.txt')
    fixed_cmake_file_name = os.path.join(os.path.split(args.cmake)[0], "fixed_"+os.path.split(args.cmake)[1])
    createCMakeLists(fixed_cmake_file_name, component_folder, all_sub_folders, all_files, root_requires)

