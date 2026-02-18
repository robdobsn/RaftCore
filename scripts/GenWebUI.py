#!/usr/bin/env python3

import argparse
import glob
import logging
from pathlib import Path
import os
import subprocess
import gzip
import sys

# Script to generate WebUI with a parcel build
# Rob Dobson 2024

logging.basicConfig(format="[%(asctime)s] %(levelname)s:%(name)s: %(message)s",
                    level=logging.INFO)
_log = logging.getLogger(__name__ if __name__ != '__main__' else Path(__file__).name)

def dir_path(string):
    if os.path.isdir(string):
        return string
    else:
        _log.error(f"GenWebUI ERROR: directory '{string}' does not exist. "
                   f"Check your systype's WebUI path configuration.")
        raise NotADirectoryError(f"Directory not found: {string}")

def parseArgs():
    parser = argparse.ArgumentParser(description="Generate Web UI")
    parser.add_argument('source',
                        nargs='?',
                        type=dir_path,
                        help="folder containing input files")
    parser.add_argument('dest',
                        nargs='?',
                        type=dir_path,
                        help="folder to output files to")
    parser.add_argument('--nogzip', action='store_false', dest='gzipContent',
                        help='gzip the resulting files')
    parser.add_argument('--npminstall', action='store_false', dest='npmInstall',
                        help='npm install in the source folder first')
    parser.add_argument('--distFolder', default='dist',
                        help='folder that npm run build builds into (relative to source folder)')
    parser.add_argument('--incmap', action='store_true', dest='includemapfiles',
                        help='include js source map files')
    return parser.parse_args()

def generateWebUI(sourceFolder, destFolder, gzipContent, distFolder, npmInstall, includemapfiles):

    _log.info("GenWebUI source '%s' dest '%s' gzip %s map %s", 
                        sourceFolder, 
                        destFolder, 
                        "Y" if gzipContent else "N",
                        "Y" if includemapfiles else "N")

    # Empty the dest folder
    try:
        _log.info(f"Emptying {destFolder}")
        for fname in os.listdir(destFolder):
            _log.info(f"Removing {os.path.join(destFolder, fname)}")
            os.remove(os.path.join(destFolder, fname))
    except:
        pass

    # If npmInstall is true, execute npm install in the source folder
    if npmInstall:
        rslt = subprocess.run(["npm", "install"], cwd=sourceFolder)
        if rslt.returncode != 0:
            _log.error("GenWebUI failed to npm install")
            return rslt.returncode
        
    # Execute npm run build in the source folder
    # Copy the resulting files to the destination folder
    rslt = subprocess.run(["npm", "run", "build"], cwd=sourceFolder)
    if rslt.returncode != 0:
        _log.error("GenWebUI failed to build Web UI")
        return rslt.returncode
    
    # Locate the npm run build output folder
    buildFolder = os.path.join(sourceFolder, distFolder)

    # Files to include in the destination folder
    extensions_to_include = ['.html', '.js', '.css']
    extensions_to_zip = []
    if gzipContent:
        extensions_to_zip = ['html', '.js', '.css']
    if includemapfiles:
        extensions_to_include.append('.map')
        if gzipContent:
            extensions_to_zip.append('.map')
    for fname in os.listdir(buildFolder):
        lower_case_fname = fname.lower()
        if lower_case_fname.endswith(tuple(extensions_to_include)):
            if lower_case_fname.endswith(tuple(extensions_to_zip)):
                with open(os.path.join(buildFolder, fname), 'rb') as f:
                    renderedStr = f.read()
                with gzip.open(os.path.join(destFolder, fname + '.gz'), 'wb') as f:
                    f.write(renderedStr)
                _log.info(f"GenWebUI created Web UI from {fname} to {fname}.gz")
            else:
                os.rename(os.path.join(buildFolder, fname), os.path.join(destFolder, fname))
                _log.info(f"GenWebUI created Web UI from {fname} to {fname}")
    return 0

def main():
    args = parseArgs()
    # Validate source folder exists and contains a package.json
    if args.source is None or not os.path.isdir(args.source):
        _log.error(f"GenWebUI ERROR: source folder '{args.source}' does not exist or was not specified. "
                   f"Ensure the WebUI source path is configured correctly in your systype.")
        return 1
    if not os.path.isfile(os.path.join(args.source, "package.json")):
        _log.error(f"GenWebUI ERROR: source folder '{args.source}' does not contain a package.json file. "
                   f"Expected a WebUI project with package.json at this location.")
        return 1
    return generateWebUI(args.source, args.dest, args.gzipContent, args.distFolder, 
                            args.npmInstall, args.includemapfiles)

if __name__ == '__main__':
    rslt = main()
    sys.exit(rslt)
    
