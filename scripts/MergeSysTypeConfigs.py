#!/usr/bin/env python3

"""Merge SysType configuration files (sdkconfig.defaults and SysTypes.json)
   with Common base and systype-specific overrides.
   
   Rob Dobson 2025

   sdkconfig.defaults merging:
     - Start with Common/sdkconfig.defaults as a base (if it exists)
     - Apply systype-specific sdkconfig.defaults overrides on top
     - Later entries override earlier ones (by CONFIG key)
     - Comments and blank lines from both files are preserved

   SysTypes.json merging:
     - Start with Common/SysTypes.json as a base (if it exists)
     - Replace/add top-level keys from systype-specific SysTypes.json
     - This is a shallow merge at the top-level key level only

"""

import argparse
import json
import os
import re
import sys

def merge_sdkconfig_defaults(common_file, systype_file, output_file):
    """Merge two sdkconfig.defaults files.
    
    Reads values from both files. The systype-specific file takes precedence.
    The output preserves comments and blank lines from both, with a clear
    separation between Common base and systype-specific overrides.
    
    Args:
        common_file: Path to Common/sdkconfig.defaults (may not exist)
        systype_file: Path to systype-specific sdkconfig.defaults (may not exist)
        output_file: Path to write the merged result
    """
    
    def parse_sdkconfig(filepath):
        """Parse an sdkconfig.defaults file into (key->value dict, raw lines list)."""
        entries = {}
        lines = []
        if filepath and os.path.exists(filepath):
            with open(filepath, 'r') as f:
                lines = f.readlines()
            for line in lines:
                stripped = line.strip()
                # Match CONFIG_XXX=value or CONFIG_XXX (no value)
                m = re.match(r'^(CONFIG_\w+)=(.*)', stripped)
                if m:
                    entries[m.group(1)] = m.group(2)
                # Also match "# CONFIG_XXX is not set" pattern
                m2 = re.match(r'^#\s*(CONFIG_\w+)\s+is not set', stripped)
                if m2:
                    entries[m2.group(1)] = None  # None means "not set"
        return entries, lines

    common_entries, common_lines = parse_sdkconfig(common_file)
    systype_entries, systype_lines = parse_sdkconfig(systype_file)

    # Build the merged entries: common as base, systype overrides
    merged_entries = {}
    merged_entries.update(common_entries)
    merged_entries.update(systype_entries)

    # Keys that the systype overrides
    overridden_keys = set(common_entries.keys()) & set(systype_entries.keys())

    # Write output: Common lines first (skipping overridden keys), then systype lines
    with open(output_file, 'w') as f:
        if common_lines:
            f.write("#\n# Base configuration from Common/sdkconfig.defaults\n#\n\n")
            for line in common_lines:
                stripped = line.strip()
                # Check if this line sets a key that is overridden
                skip = False
                m = re.match(r'^(CONFIG_\w+)=', stripped)
                if m and m.group(1) in overridden_keys:
                    skip = True
                m2 = re.match(r'^#\s*(CONFIG_\w+)\s+is not set', stripped)
                if m2 and m2.group(1) in overridden_keys:
                    skip = True
                if not skip:
                    f.write(line)

        if systype_lines:
            if common_lines:
                f.write("\n#\n# SysType-specific overrides\n#\n\n")
            for line in systype_lines:
                f.write(line)

    # Ensure file ends with newline
    with open(output_file, 'a') as f:
        f.write("")


def merge_systypes_json(common_file, systype_file, output_file):
    """Merge two SysTypes.json files with shallow top-level key merge.
    
    Start with Common/SysTypes.json, then replace/add top-level keys
    from the systype-specific SysTypes.json.
    
    Args:
        common_file: Path to Common/SysTypes.json (may not exist)
        systype_file: Path to systype-specific SysTypes.json (may not exist)
        output_file: Path to write the merged result
    """
    common_data = {}
    systype_data = {}

    if common_file and os.path.exists(common_file):
        with open(common_file, 'r') as f:
            common_data = json.load(f)

    if systype_file and os.path.exists(systype_file):
        with open(systype_file, 'r') as f:
            systype_data = json.load(f)

    # Shallow merge: systype top-level keys override common
    merged = {}
    merged.update(common_data)
    merged.update(systype_data)

    with open(output_file, 'w') as f:
        json.dump(merged, f, indent=4)
        f.write('\n')


def main():
    parser = argparse.ArgumentParser(
        description="Merge SysType configuration files (Common base + systype overrides)")
    parser.add_argument('--mode', required=True, choices=['sdkconfig', 'systypes'],
                        help="Merge mode: 'sdkconfig' for sdkconfig.defaults, 'systypes' for SysTypes.json")
    parser.add_argument('--common', default=None,
                        help="Path to the Common base file (optional, may not exist)")
    parser.add_argument('--systype', default=None,
                        help="Path to the systype-specific file (optional, may not exist)")
    parser.add_argument('--output', required=True,
                        help="Path to write the merged output file")

    args = parser.parse_args()

    # Normalize paths - treat empty strings and non-existent files consistently
    common = args.common if args.common and os.path.exists(args.common) else None
    systype = args.systype if args.systype and os.path.exists(args.systype) else None

    if not common and not systype:
        print(f"Warning: Neither common nor systype file exists for {args.mode} merge", file=sys.stderr)
        # Create an empty output file
        with open(args.output, 'w') as f:
            if args.mode == 'systypes':
                f.write('{}\n')
        return

    if args.mode == 'sdkconfig':
        merge_sdkconfig_defaults(common, systype, args.output)
    elif args.mode == 'systypes':
        merge_systypes_json(common, systype, args.output)

    # Report what was merged
    if common and systype:
        print(f"Merged {args.mode}: Common + SysType-specific -> {args.output}")
    elif common:
        print(f"Using {args.mode} from Common (no systype-specific file) -> {args.output}")
    elif systype:
        print(f"Using {args.mode} from SysType-specific (no Common base) -> {args.output}")


if __name__ == '__main__':
    main()
