#!/usr/bin/env python3

""" Raft system configuration files contain JSON and are located in the SysTypes folder inside the folder for
    a specific build type in the buildConfigs folder
    This script converts the JSON files in that folder into a C++ header file that can be included in the
    main project
    The files must be named based on their SysType and end with .json (for instance "MySystem.json" - where
    the SysType is "MySystem")
    In addition, if hardware revision selection is used then additional JSON files may be added, one for each
    hardware revision. These files must be named based on their SysType and end with "#hwrev<N>.json" (where
    <N> is the hardware revision number which can be multiple digits long) - e.g. "MySystem#hwrev1.json", 
    "MySystem#hwrev2.json" - note that hardware revision 0 is the default and uses the base name "MySystem.json"
"""

import argparse
import json
import logging
import os
import re
import sys
from typing import Any, Dict, List, Set, Tuple, Union

logging.basicConfig(format="[%(asctime)s] %(levelname)s:%(name)s: %(message)s",
                level=logging.INFO)
_log = logging.getLogger(__name__ if __name__ != '__main__' else os.path.basename(__file__))

def parseArgs():
    parser = argparse.ArgumentParser(description="Convert JSON to C Header")
    parser.add_argument('inFolder',
                        type=str,
                        help="Input folder")                        
    parser.add_argument('outFile',
                        nargs='?',
                        type=argparse.FileType('w'),
                        default=sys.stdout,
                        help="C .h output file")
    return parser.parse_args()

def file_sort_criteria(item):
    # Extract the hardware revision number from the filename if present
    srch_rslt = re.search(r'#hwrev(\d+)\.json$', item)
    if srch_rslt:
        # Rebuild the filename with the hardware revision number expanded to 4 digits
        return re.sub(r'#hwrev(\d+)\.json$', r'#hwrev{:04d}.json'.format(int(srch_rslt.group(1))), item)
    # Add the hardware revision number to the end of the filename
    return item.split('.')[0] + "#hwrev0000.json"

def genHeaderFileFromJSON(inFolder, outFile) -> None:
    # Generate header from JSON
    try:
        # Iterate over JSON files in the input folder
        isFirst = True
        # print(f"Processing folder {inFolder} - sorted file list: {sorted(os.listdir(inFolder), key=file_sort_criteria)}")
        for filename in sorted(os.listdir(inFolder), key=file_sort_criteria):
            if filename.endswith(".json"):
                # Split the filename on the #hwrev if present to form SysType and hardware revision
                split_name = re.split(r'#hwrev\d+\.json$', filename)
                if len(split_name) == 1:
                    # No hardware revision
                    sys_type = split_name[0]
                    hw_rev = 0
                else:
                    # Hardware revision
                    sys_type = split_name[0]
                    hw_rev = int(re.search(r'#hwrev(\d+)\.json$', filename).group(1))

                # Debug
                print(f"... Generating SysTypes header with {sys_type} hardware revision {hw_rev} from {filename}")

                # Read in the JSON file
                with open(os.path.join(inFolder, filename), 'r') as inFile:
                    
                    # Parse the JSON
                    sys_type_json = json.load(inFile)

                    # If this is not the first then add a comma to the output
                    if not isFirst:
                        outFile.write(',\n')
                    isFirst = False

                    # Opening brace for the JSON
                    outFile.write('{\n')

                    # Write the SysType name to the header file
                    outFile.write(f'    R"({sys_type})",\n')
                    outFile.write(f'    {hw_rev},\n')

                    # Write the JSON to the header file
                    sys_type_lines = json.dumps(sys_type_json,separators=(',', ':'),indent="    ").splitlines()
                    for line in sys_type_lines:
                        indentGrp = re.search(r'^\s*', line)
                        indentText = indentGrp.group(0) if indentGrp else ""
                        outFile.write(f'    {indentText}R"({line.strip()})"\n')

                    # Closing brace for the JSON
                    outFile.write('}')
    except ValueError as excp:
        _log.error(f"Failed JSON parsing of SysTypes JSON error {excp}")
        sys.exit(1)

def main():
    args = parseArgs()
    genHeaderFileFromJSON(args.inFolder, args.outFile)

if __name__ == '__main__':
    main()
