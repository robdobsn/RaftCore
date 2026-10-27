#!/usr/bin/env python3

"""Merge SysType configuration files (sdkconfig.defaults and SysTypes.json)
   with Common base and systype-specific overrides.
   
   Rob Dobson 2025

   sdkconfig.defaults merging:
     - Start with Common/sdkconfig.defaults as a base (if it exists)
     - Apply any sdkconfig fragments (RAFT_SDKCONFIG_FRAGMENTS in features.cmake) in order
     - Apply systype-specific sdkconfig.defaults overrides on top
     - Later entries override earlier ones (by CONFIG key)
     - Comments and blank lines from all files are preserved

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


def line_config_key(line):
    """The CONFIG key a line sets (CONFIG_X=... or # CONFIG_X is not set), or None."""
    stripped = line.strip()
    m = re.match(r'^(CONFIG_\w+)=', stripped)
    if m:
        return m.group(1)
    m2 = re.match(r'^#\s*(CONFIG_\w+)\s+is not set', stripped)
    if m2:
        return m2.group(1)
    return None


def merge_sdkconfig_layers(layers, output_file):
    """Merge sdkconfig.defaults layers, each later layer overriding earlier ones (by CONFIG key).

    The output preserves the comments and blank lines of every layer, each under a heading, and leaves out
    lines of a layer which set a key that a later layer sets.

    Args:
        layers: list of (heading, path) - heading is None for a layer only headed when it follows another
                layer with content (the SysType-specific overrides). Paths may not exist
        output_file: Path to write the merged result
    """
    parsed = [(heading, parse_sdkconfig(path)) for heading, path in layers]

    with open(output_file, 'w') as f:
        any_written = False
        for idx, (heading, (entries, lines)) in enumerate(parsed):
            if not lines:
                continue

            # Keys set by any later layer
            later_keys = set()
            for _, (later_entries, _) in parsed[idx + 1:]:
                later_keys.update(later_entries.keys())

            if heading:
                f.write(("\n" if any_written else "") + f"#\n# {heading}\n#\n\n")
            elif any_written:
                f.write("\n#\n# SysType-specific overrides\n#\n\n")
            for line in lines:
                if line_config_key(line) in later_keys:
                    continue
                f.write(line)
            any_written = True


def merge_sdkconfig_defaults(common_file, systype_file, output_file, fragment_files=None):
    """Merge sdkconfig.defaults files: Common base, then fragments, then systype-specific overrides.

    Args:
        common_file: Path to Common/sdkconfig.defaults (may not exist)
        systype_file: Path to systype-specific sdkconfig.defaults (may not exist)
        output_file: Path to write the merged result
        fragment_files: Paths of sdkconfig fragments applied between them, in order
    """
    layers = [("Base configuration from Common/sdkconfig.defaults", common_file)]
    for fragment_file in fragment_files or []:
        layers.append((f"sdkconfig fragment {os.path.basename(fragment_file)}", fragment_file))
    layers.append((None, systype_file))
    merge_sdkconfig_layers(layers, output_file)


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
    parser.add_argument('--fragment', action='append', default=[],
                        help="Path to an sdkconfig fragment applied after Common and before the SysType "
                             "(sdkconfig mode only, may be repeated, must exist)")
    parser.add_argument('--output', required=True,
                        help="Path to write the merged output file")

    args = parser.parse_args()

    # Normalize paths - treat empty strings and non-existent files consistently
    common = args.common if args.common and os.path.exists(args.common) else None
    systype = args.systype if args.systype and os.path.exists(args.systype) else None
    for fragment in args.fragment:
        if not os.path.exists(fragment):
            print(f"Error: sdkconfig fragment {fragment} not found", file=sys.stderr)
            sys.exit(1)

    if not common and not systype and not args.fragment:
        print(f"Warning: Neither common nor systype file exists for {args.mode} merge", file=sys.stderr)
        # Create an empty output file
        with open(args.output, 'w') as f:
            if args.mode == 'systypes':
                f.write('{}\n')
        return

    if args.mode == 'sdkconfig':
        merge_sdkconfig_defaults(common, systype, args.output, args.fragment)
        for fragment in args.fragment:
            print(f"Applied sdkconfig fragment {fragment}")
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
