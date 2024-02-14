import argparse
import json
import os.path

# Each library project requires a number of files to be maintained with the same information:
# - CMakeLists.txt in the root folder
# - component.mk in the root folder
# - library.json

# At least the CMakeLists.txt and library/.json are required to be present initially

# This script starts with the CMakeLists.txt in the root folder which is considered the master file
# It extracts the main sections from this file
# It then checks the other files against the master file
# If changes are needed then files called new_component.mk and new_library.json are created with the
# updated information (this assumes the --force option is not specified - if it is then the original files
# are overwritten)

# Parse CMakeLists.txt
def parse_cmakelists(cmake_file_name):

    print(f'Parsing {cmake_file_name}')
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

    print(f"Component base folder: {component_folder}")
    print(f'Files:', files)
    print(f'Includes:', includes)
    print(f'Requires:', requires)

    return files, includes, requires, component_folder

def gen_new_libraryjson(library_json_file_name, new_library_json_file_name, component_folder, includes):
    print(f'Generating {new_library_json_file_name}')
    # Open existing library.json and create a copy
    with open(library_json_file_name, 'r') as f:
        library_json = json.load(f)
        fixed_library_json = library_json.copy()
    # Extract build flags from library.json
    build_flags = library_json['build']['flags']
    # Extract include flags and remove them from the copy
    lib_included_folders = [x[2:] for x in build_flags if x.startswith('-I')]
    fixed_library_json['build']['flags'] = [x for x in build_flags if not x.startswith('-I')]
    # Replace the include flags with the new ones
    for folder in includes:
        full_path_folder = os.path.join(component_folder, folder)
        fixed_library_json['build']['flags'].append(f'-I{full_path_folder}')
    # Generate the srcFilter entry
    src_filter = ["-<*>"]
    for folder in includes:
        src_filter.append(f"+<{os.path.join(component_folder, folder)}>")
    fixed_library_json['srcFilter'] = src_filter
    # Set the includeDir and srcDir to the component folder
    fixed_library_json['includeDir'] = "."
    fixed_library_json['srcDir'] = "."
    # Store the new library.json
    with open(new_library_json_file_name, 'w') as f:
        json.dump(fixed_library_json, f, indent=2)

def gen_new_componentmk(component_mk_file_name, new_component_mk_file_name, component_folder, includes):
    print(f'Generating {new_component_mk_file_name}')
    fixed_component_mk = []
    # If there is an existing file then create a copy
    if os.path.isfile(component_mk_file_name):
        with open(component_mk_file_name, 'r') as f:
            component_mk = f.readlines()
            fixed_component_mk = component_mk.copy()
    # Remove lines which define include and source folders
    fixed_component_mk = [x for x in fixed_component_mk if not x.startswith('COMPONENT_ADD_INCLUDEDIRS')]
    fixed_component_mk = [x for x in fixed_component_mk if not x.startswith('COMPONENT_SRCDIRS')]
    # Add the new include and source folders
    full_path_folders = [os.path.join(component_folder, folder) for folder in includes]
    fixed_component_mk.append(f'COMPONENT_ADD_INCLUDEDIRS := {" ".join(full_path_folders)}\n')
    fixed_component_mk.append(f'COMPONENT_SRCDIRS {" ".join(full_path_folders)}\n')
    # Store the new component.mk
    with open(new_component_mk_file_name, 'w') as f:
        f.writelines(fixed_component_mk)
        
if __name__ == "__main__":
    print('-----------')
    print('Cross-checking build files CMakeLists.txt, library.json and component.mk')
    print('CMakeLists.txt is the master file and other files are fixed to match it')
    print('-----------')
    parser = argparse.ArgumentParser(description='Compare imports in library.json and CMakeFile.txt')
    parser.add_argument('-l', '--lib', type=str, help='path to library.json', default='library.json')
    parser.add_argument('-c', '--cmake', type=str, help='path to CMakeLists.txt', default='CMakeLists.txt')
    parser.add_argument('-m', '--componentmk', type=str, help='path to component.mk', default='component.mk')
    parser.add_argument('-f', '--force', action='store_true', help='overwrite existing files')
    args = parser.parse_args()

    # Check that the CMakelists.txt exists
    if not os.path.isfile(args.cmake):
        print(f'-----------\nCMakeLists.txt does not exist: {args.cmake}')
        exit(1)

    # Check that the library.json exists
    if not os.path.isfile(args.lib):
        print(f'-----------\nlibrary.json does not exist: {args.lib}')
        exit(1)

    # Parse the CMakeLists.txt
    root_files, root_includes, root_requires, component_folder = parse_cmakelists(args.cmake)
    print(f'Root files: {root_files}')
    print(f'Root includes: {root_includes}')
    print(f'Root requires: {root_requires}')
    print(f'Component folder: {component_folder}')

    # Work out the file name for the new library.json
    if args.force:
        new_library_json_file_name = args.lib
    else:
        new_library_json_file_name = os.path.join(os.path.split(args.lib)[0], "new_"+os.path.split(args.lib)[1])

    # Generate a new library.json file with the correct includes
    gen_new_libraryjson(args.lib, new_library_json_file_name, component_folder, root_includes)

    # Work out the file name for the new component.mk
    if not args.force and os.path.isfile(args.componentmk):
        new_component_mk_file_name = os.path.join(os.path.split(args.componentmk)[0], "new_"+os.path.split(args.componentmk)[1])
    else:
        new_component_mk_file_name = args.componentmk

    # Generate a new component.mk file with the correct includes
    gen_new_componentmk(args.componentmk, new_component_mk_file_name, component_folder, root_includes)

    # # Extract include flags from library.json
    # with open(args.lib, 'r') as f:
    #     library_json = json.load(f)
    #     fixed_library_json = library_json.copy()
    # build_flags = library_json['build']['flags']
    # lib_included_folders = [x[2:] for x in build_flags if x.startswith('-I')]

    # # Extract sections from root CMakeLists.txt
    # root_files, root_includes, root_requires, component_folder = parseCMakeLists(args.cmake)

    # # Check that all included folders in library.json are in the same component folder
    # for folder in lib_included_folders:
    #     if not folder.startswith(component_folder):
    #         print(f'-----------\nlibrary.json included folders are not all in the same component folder: {folder} is not in {component_folder}')
    #         exit(1)

    # # Extract sub-folder names
    # lib_sub_folders = [x[len(component_folder) + 1:] if len(x) > len(component_folder) else '.' for x in lib_included_folders]
    # print(f'-----------\nSub-folders in library.json: {lib_sub_folders}')

    # # Create sets of all sub folders
    # all_sub_folders = set([os.path.split(x)[0] for x in root_files if len(os.path.split(x)[0]) > 0])
    # all_sub_folders.update([x for x in root_includes])
    # all_sub_folders.update(lib_sub_folders)

    # # Create set of all files
    # all_files = set(root_files)

    # # Check if there is a CMakelists.txt in the component folder
    # component_cmake_file_name = os.path.join(component_folder, 'CMakeLists.txt')
    # component_files = []
    # component_includes = []
    # if os.path.isfile(component_cmake_file_name):

    #     # Extract sections from the component CMakeLists.txt
    #     component_files, component_includes, _, _ = parseCMakeLists(component_cmake_file_name)

    #     # Add to the set of folders and files
    #     all_sub_folders.update([os.path.split(x)[0] for x in component_files if len(os.path.split(x)[0]) > 0])
    #     all_sub_folders.update([x for x in component_includes])
    #     all_files.update(component_files)

    # # Debug
    # print(f'-----------\nAll sub folders in all places: {all_sub_folders}')
    # print(f'-----------\nAll files in all places: {all_files}')

    # print(f'====================== CHECKS ==========================')

    # # Check that all sub-folders exist
    # folders_to_remove = []
    # for folder in all_sub_folders:
    #     full_path_folder = os.path.join(component_folder, folder)
    #     if not os.path.isdir(full_path_folder):
    #         print(f'Sub-folder {folder} does not exist')
    #         folders_to_remove.append(folder)
    #         fix_required = True
    # for folder in folders_to_remove:
    #     all_sub_folders.remove(folder)

    # # Check that all sub-folders are included in the JSON file
    # fix_required = False
    # for folder in all_sub_folders:
    #     if folder not in lib_sub_folders: 
    #         print(f'Sub-folder {folder} is not included in library.json')
    #         fix_required = True
    #         full_path_folder = os.path.join(component_folder, folder)
    #         fixed_library_json['build']['flags'].append(f'-I{full_path_folder}')

    # # Check that all files are included in the various CMakeLists.txt
    # for folder in all_sub_folders:
    #     if folder not in root_includes:
    #         print(f'File {folder} is not included in root CMakeLists.txt')
    #         fix_required = True
    # for file in all_files:
    #     if file not in root_files:
    #         print(f'File {file} is not included in root CMakeLists.txt')
    #         fix_required = True
    #     if file not in component_files:
    #         print(f'File {file} is not included in component CMakeLists.txt')
    #         fix_required = True

    # print(f'==================== END CHECKS ========================')

    # # Check that component folder has a CMakeLists.txt
    # component_cmake_file_name = os.path.join(component_folder, 'CMakeLists.txt')
    # if not os.path.isfile(component_cmake_file_name):
    #     # create a CMakeLists.txt in the component folder
    #     createCMakeLists(component_cmake_file_name, "", all_sub_folders, all_files, root_requires)
    # elif fix_required:
    #     # create a fixed_CMakeLists.txt in the component folder
    #     fixed_cmake_file_name = os.path.join(component_folder, 'fixed_CMakeLists.txt')
    #     createCMakeLists(fixed_cmake_file_name, "", all_sub_folders, all_files, root_requires)

    # # Check if fix required
    # if fix_required:
    #     # Sort the includes
    #     fixed_library_json['build']['flags'] = sorted(fixed_library_json['build']['flags'], key=str.casefold)
    #     # Write fixed library.json
    #     print(f'-----------\nWriting fixed library.json')
    #     fixed_library_json_file_name = os.path.join(os.path.split(args.lib)[0], "fixed_"+os.path.split(args.lib)[1])
    #     with open(fixed_library_json_file_name, 'w') as f:
    #         json.dump(fixed_library_json, f, indent=2)

    #     # Write fixed CMakeLists.txt in root folder
    #     print(f'-----------\nWriting fixed CMakeLists.txt')
    #     fixed_cmake_file_name = os.path.join(os.path.split(args.cmake)[0], "fixed_"+os.path.split(args.cmake)[1])
    #     createCMakeLists(fixed_cmake_file_name, component_folder, all_sub_folders, all_files, root_requires)

