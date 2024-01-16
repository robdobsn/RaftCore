#!/usr/bin/env python3

""" Raft system configuration files contain JSON

    A common SysTypes.json file can be present in the buildConfigs/<specific-build-type> folder and this 
    file can include versioned keys (see below for details of versioning)

    This script converts the JSON file in that folder into a C++ file that can be included in the
    main project

    Versioning of JSON

    The JSON file can contain versioned keys.  The version name (or number) is appended to the key name
    with a '##' separator.  For example, the key 'key' with version 'NEW_HARDWARE' would be written as
    'key##NEW_HARDWARE'.  The version name can be any string but must not contain the '##' separator and
    regular keys cannot contain the '##' separator. A versioned key can contain more than one version
    name.  For example, the key 'key' with versions 'NEW_HARDWARE' and 'OLD_HARDWARE' would be written as
    'key##NEW_HARDWARE##OLD_HARDWARE'.
"""

import argparse
import json
import logging
import os
import re
import sys
import copy
from typing import Any, Dict, List, Set, Tuple, Union

logging.basicConfig(format="[%(asctime)s] %(levelname)s:%(name)s: %(message)s",
                level=logging.INFO)
_log = logging.getLogger(__name__ if __name__ != '__main__' else os.path.basename(__file__))

def parseArgs():
    parser = argparse.ArgumentParser(description="Convert JSON to CPP with key versioning")
    parser.add_argument('inFile',
                        type=argparse.FileType('r'),
                        help="JSON input file")
    parser.add_argument('outFile',
                        nargs='?',
                        type=argparse.FileType('w'),
                        default=sys.stdout,
                        help="C .h output file")
    parser.add_argument('--cpp_template',
                        type=argparse.FileType('r'),
                        help="C++ template file")
    return parser.parse_args()

# Recurse through a JSON obect and find all the versioned keys
def find_versioned_keys(json_obj: Union[Dict, List], versioned_keys: Set[str], 
                                sys_type_name_prefix: str, sys_type_names: Dict) -> None:
    if isinstance(json_obj, dict):
        for key, val in json_obj.items():
            if re.search(r'##', key):
                versioned_keys.add(key)
                # Check if this is a SysTypeName key
                if key.split('##')[0] == sys_type_name_prefix:
                    for sys_type in key.split('##')[1:]:
                        sys_type_names[sys_type] = val
            if isinstance(val, dict) or isinstance(val, list):
                find_versioned_keys(val, versioned_keys, sys_type_name_prefix, sys_type_names)
    elif isinstance(json_obj, list):
        for val in json_obj:
            if isinstance(val, dict) or isinstance(val, list):
                find_versioned_keys(val, versioned_keys, sys_type_name_prefix, sys_type_names)

# Recurse through the JSON object and remove all versioned keys that don't match the version name
def remove_versioned_keys(json_obj: Union[Dict, List], version_name: str) -> bool:
    if isinstance(json_obj, dict):
        for key, val in json_obj.items():
            if re.search(r'##', key):
                if version_name not in key.split('##')[1:]:
                    # print(f"Removing key {key} from JSON value {json_obj[key]}")
                    del json_obj[key]
                    return True
                else:
                    # print(f"Replacing key {key} with {key.split('##')[0]} in JSON value {json_obj[key]}")
                    json_obj[key.split('##')[0]] = json_obj[key]
                    del json_obj[key]
                    return True
            if isinstance(val, dict) or isinstance(val, list):
                if remove_versioned_keys(val, version_name):
                    return True
    elif isinstance(json_obj, list):
        for val in json_obj:
            if isinstance(val, dict) or isinstance(val, list):
                if remove_versioned_keys(val, version_name):
                    return True

def genCppFileFromJSON(inFile, outFile, template) -> None:
    # Generate cpp from JSON
    try:

        # Read in the JSON file
        sys_type_json = json.load(inFile)

        # Find all the versioned keys
        versioned_keys = set()
        sys_type_names = {}
        find_versioned_keys(sys_type_json, versioned_keys, "SysTypeName", sys_type_names)
        # print(f"Versioned keys: {versioned_keys}")

        # Extract unique version names
        version_names = set()
        for key in versioned_keys:
            version_names.add(key.split('##')[1])
        # print(f"Version names: {version_names}")

        # Set sys_type_names entry for keys that don't have a SysTypeName
        for key in version_names:
            if key not in sys_type_names:
                sys_type_names[key] = key
        # print(f"SysType names: {sys_type_names}")
                
        # Check if there are no versioned keys
        if len(versioned_keys) == 0:
            # Add a default version name
            version_names.add("<<DEFAULT>>")
            # Set sys_type_names entry for keys that don't have a SysTypeName
            sys_type_names["<<DEFAULT>>"] = "<<DEFAULT>>"

        # Sort the version names
        version_names_sorted = sorted(version_names)

        # Write lines to the cpp file to indicate that is is auto-generated
        outFile.write(f"// This file is auto-generated from {inFile.name}\n")
        outFile.write(f"// Do not edit this file\n\n")

        # If a template file was specified then extract what should be written before
        # and after the generated code
        if template:
            pre_data_lines = []
            post_data_lines = []
            is_pre_data = True
            for line in template:
                if "{{GENERATED_CODE}}" in line:
                    is_pre_data = False
                elif is_pre_data:
                    pre_data_lines.append(line)
                else:
                    post_data_lines.append(line)
            for line in pre_data_lines:
                outFile.write(line)

        # Create an unversioned object for each version name
        isFirst = True
        for version_name in version_names_sorted:

            # Create a copy of the JSON object
            unversioned_sys_type_json = copy.deepcopy(sys_type_json)

            # Remove all versioned keys that don't match the version name
            while remove_versioned_keys(unversioned_sys_type_json, version_name):
                pass

            # # Debug write the json object to a debug file
            # with open(f"sys_type_{version_name}.json", 'w') as debug_file:
            #     json.dump(unversioned_sys_type_json, debug_file, indent=4)

            # Debug
            print(f"... Generating SysTypes cpp with version {version_name}")

            # If this is not the first then add a comma to the output
            if not isFirst:
                outFile.write(',\n')
            isFirst = False

            # Opening brace for the JSON
            outFile.write('{\n')

            # Write the SysType name and version name to the cpp file
            outFile.write(f'    R"({sys_type_names[version_name]})",\n')
            outFile.write(f'    R"({version_name})",\n')

            # Write the JSON to the cpp file
            sys_type_lines = json.dumps(unversioned_sys_type_json, separators=(',', ':'),indent="    ").splitlines()
            for line in sys_type_lines:
                indentGrp = re.search(r'^\s*', line)
                indentText = indentGrp.group(0) if indentGrp else ""
                outFile.write(f'    {indentText}R"({line.strip()})"\n')

            # Closing brace for the JSON
            outFile.write('}')

        # If a template file was specified then extract what should be written before
        # and after the generated code
        if template:
            for line in post_data_lines:
                outFile.write(line)

    except ValueError as excp:
        _log.error(f"Failed JSON parsing of SysTypes JSON error {excp}")
        sys.exit(1)

def main():
    args = parseArgs()
    genCppFileFromJSON(args.inFile, args.outFile, args.cpp_template)

if __name__ == '__main__':
    main()
