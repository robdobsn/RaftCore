import argparse
import json
import os.path
import re
import shutil

__version__ = "2.0.1"

# Each library project requires a number of files to be maintained:
# - CMakeLists.txt in the root folder
# - library.json - used by PlatformIO
# - library.properties - used by Arduino IDE
# - <libname>.h - in the src/ folder and including all the other header files (for Arduino IDE)

# At least the CMakeLists.txt and library.json are required to be present initially

# This script starts with the CMakeLists.txt in the root folder which is considered the master file
# It extracts the main sections from this file
# It then checks the other files against the master file
# If changes are needed (or --force option) then files are created with the updated information 
# --force generates files as needed

class LibBuildFileManager:

    def __init__(self, cmake_file_name, lib_json_file_name, lib_props_file_name, libname_default):
        # Args
        self.cmake_file_name = cmake_file_name
        self.lib_json_file_name = lib_json_file_name
        self.lib_props_file_name = lib_props_file_name
        self.main_include_file_name = libname_default

        # Extracted data
        self.library_name = ""
        self.cmakelists_sections = {
            "component_base_folder": "",
            "files": [],
            "includes": [],
            "var_includes": [],
            "requires": []
        }
        self.lib_props_lines = []
        self.main_include_file_lines = []

    def path_join(self, path1, path2):
        return os.path.join(path1, path2).replace("\\", "/")
    
    # Parse CMakeLists.txt
    def parse_cmakelists(self):

        with open(self.cmake_file_name, 'r') as f:
            cmakelists_lines = f.readlines()
        in_srcs_section = False
        in_include_section = False
        in_require_section = False
        files = self.cmakelists_sections["files"]
        includes = self.cmakelists_sections["includes"]
        var_includes = self.cmakelists_sections["var_includes"]
        requires = self.cmakelists_sections["requires"]
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
                if "${" in include_path:
                    var_includes.append(include_path)
                else:
                    includes.append(include_path)
            elif in_require_section:
                requires.append(line.strip())

        # Discover the component folder
        component_folder = ""
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
        self.cmakelists_sections["component_base_folder"] = component_folder

        # Remove component folder from file names and include paths
        if component_folder != '':
            self.cmakelists_sections["files"] = [re.sub(r'^'+component_folder+'/', '', x) for x in files]
            self.cmakelists_sections["includes"] = [re.sub(r'^'+component_folder+'/', '', x) for x in includes]

    # Parse library.json
    def parse_library_json(self):
        with open(self.lib_json_file_name, 'r') as f:
            self.library_json = json.load(f)
            self.fixed_library_json = None
            self.library_name = self.library_json.get("name", "UNKNOWN")
            if self.main_include_file_name == "<libname>.h":
                self.main_include_file_name = self.library_json.get("name", "")
                if self.main_include_file_name != "":
                    self.main_include_file_name += ".h"

    def check_library_json_and_fix(self, force_rebuild_includes):
        build_flags = self.library_json.get('build', {}).get('flags', "")
        lib_included_folders = [x[2:] for x in build_flags if x.startswith('-I')]
        other_build_flags = [x for x in build_flags if not x.startswith('-I')]
        # Remove base folder
        component_folder = self.cmakelists_sections.get("component_base_folder", "")
        lib_included_folders = [re.sub(r'^'+component_folder+'/', '', x) for x in lib_included_folders]
        # Check list against cmakelists
        set_of_cmake_includes = set(self.cmakelists_sections["includes"])
        set_of_lib_includes = set(lib_included_folders)
        if set_of_cmake_includes != set_of_lib_includes or force_rebuild_includes:
            if force_rebuild_includes:
                print("Forcing rebuild of includes in library.json")
            else:
                print("Includes in library.json and CMakeLists.txt do not match (len = ", len(set_of_cmake_includes), len(set_of_lib_includes), ")")
            self.fixed_library_json = self.library_json.copy()
            self.fixed_library_json['build']['flags'] = other_build_flags
            for folder in set_of_cmake_includes:
                full_path_folder = self.path_join(component_folder, folder)
                self.fixed_library_json['build']['flags'].append(f'-I{full_path_folder}')
        else:
            print("Includes in library.json and CMakeLists.txt match")

    def generate_library_properties(self):
        author_list = self.library_json.get("authors", [])
        author_name = author_list[0].get("name", "") if len(author_list) > 0 else ""
        author_email = author_list[0].get("email", "") if len(author_list) > 0 else ""
        platforms = self.library_json.get("platforms", [])
        architectures = ",".join([x.replace('"','') for x in platforms])
        lib_props = self.lib_props_lines
        lib_props.append(f'name={self.library_json.get("name", "")}')
        lib_props.append(f'version={self.library_json.get("version", "")}')
        lib_props.append(f'author={author_name} <{author_email}>')
        lib_props.append(f'maintainer={author_name} <{author_email}>')
        lib_props.append(f'sentence={self.library_json.get("description", "")}')
        lib_props.append(f'paragraph={self.library_json.get("paragraph", "")}')
        lib_props.append(f'url={self.library_json.get("repository", {}).get("url", self.library_json.get("homepage", ""))}')
        lib_props.append(f'category={self.library_json.get("category", "Uncategorized")}')
        lib_props.append(f'architectures={architectures}')
        lib_props.append(f'license={self.library_json.get("license", "unknown")}')
        if self.main_include_file_name != "":
            lib_props.append(f'includes={self.main_include_file_name}')

    def generate_main_include_file(self):
        inc_lines = self.main_include_file_lines
        inc_lines.append(f"// {self.library_name} library main include file")
        inc_lines.append(f"// This file is autogenerated - it may be overwritten")
        inc_lines.append(f"")
        inc_lines.append(f"#pragma once")
        inc_lines.append(f"")
        inc_lines.append(f"// Include all the header files")
        for include in self.cmakelists_sections["includes"]:
            inc_lines.append(f'#include "{include}"')
        inc_lines.append(f"")

    def write_files(self, nooverwrite):
        if self.fixed_library_json is not None:
            if not nooverwrite:
                print(f'Writing fixed library.json')
                with open(self.lib_json_file_name, 'w') as f:
                    json.dump(self.fixed_library_json, f, indent=2)
        if not os.path.exists(self.lib_props_file_name) or not nooverwrite:
            print(f'Writing library.properties')
            with open(self.lib_props_file_name, 'w') as f:
                f.writelines([li+"\n" for li in self.lib_props_lines])
        
    def flatten_structure_for_arduino_ide(self):
        # Arduino source folder
        arduino_src_folder = "src"
        component_folder = self.cmakelists_sections.get("component_base_folder", "")
        if arduino_src_folder == component_folder:
            print("Component folder is already src - cannot flatten structure")
            return
        
        # Flatten the structure
        print(f'Flattening structure for Arduino IDE')

        # For all files in the component_folder recursively, copy to the src folder
        for root, dirs, files in os.walk(component_folder):
            for file in files:
                src_file = os.path.join(root, file)
                dest_file = os.path.join(arduino_src_folder, file)
                print(f'Copying {src_file} to {dest_file}')
                os.makedirs(os.path.dirname(dest_file), exist_ok=True)
                # Copy file
                shutil.copy2(src_file, dest_file)

if __name__ == "__main__":
    print('-----------')
    print('Managing library build files CMakeLists.txt, library.json, library.properties and main libray include')
    print('CMakeLists.txt and library.json determine other file contents')
    print(f'Version {__version__} Rob Dobson 2020-2024')
    print('-----------')
    parser = argparse.ArgumentParser(description='Compare imports in library.json and CMakeFile.txt')
    parser.add_argument('-l', '--libjson', type=str, help='path to library.json', default='library.json')
    parser.add_argument('-c', '--cmake', type=str, help='path to CMakeLists.txt', default='CMakeLists.txt')
    parser.add_argument('-p', '--properties', type=str, help='path to library.properties', default='library.properties')
    parser.add_argument('-a', '--arduinoide', action='store_true', help='flatten file structure for Arduino IDE')
    parser.add_argument('-i', '--includename', type=str, help='name of file included when library added to Arduino project', default='<libname>.h')
    parser.add_argument('-n', '--nooverwrite', action='store_true', help='do not overwrite existing files')
    parser.add_argument('-f', '--forceincludes', action='store_true', help='force recreation of library.json includes section')
    parser.add_argument('--version', action='version', version=f'%(prog)s {__version__}')
    args = parser.parse_args()

    # Check that the CMakelists.txt exists
    if not os.path.isfile(args.cmake):
        print(f'-----------\nCMakeLists.txt does not exist: {args.cmake}')
        exit(1)

    # Check that the library.json exists
    if not os.path.isfile(args.libjson):
        print(f'-----------\nlibrary.json does not exist: {args.libjson}')
        exit(1)

    # Handler
    manager = LibBuildFileManager(args.cmake, args.libjson, args.properties, args.includename)

    # Parse the CMakeLists.txt
    manager.parse_cmakelists()

    # Parse library.json
    manager.parse_library_json()

    # Check and fix library.json
    manager.check_library_json_and_fix(args.forceincludes)

    # Generate library.properties
    manager.generate_library_properties()

    # Write out files if required
    manager.write_files(args.nooverwrite)

    # Generate flattened structure for arduino ide
    if args.arduinoide:
        manager.flatten_structure_for_arduino_ide()
