import json
import sys
import re
import argparse

from DecodeGenerator import DecodeGenerator
from MicroPythonGenerator import MicroPythonGenerator

# ProcessDevTypeJsonToC.py
# Rob Dobson 2024
# This script processes a JSON I2C device types file and generates a C header file
# with the device types and addresses. The header file contains the following:
# - An array of DeviceTypeRecord structures - these contain the device type, addresses, detection values, init values, polling config and device info
# - An array of device type counts for each address (0x00 to 0x7f) - this is the number of device types for each address
# - An array of device type indexes for each address - each element is an array of indices into the DeviceTypeRecord array
# - An array of scanning priorities for each address
# The script takes these arguments:
# - A comma separated list of paths to JSON files containing device types which may include [ at the start and ] at the end of the path
# - The path to the device type header file to generate
# - The path to the poll-record header file to generate
# The script also takes optional arguments:
# --POLL_RESULT_TIMESTAMP_SIZE - the size of the timestamp in the poll result data
# --POLL_RESULT_RESOLUTION_US - the resolution of the timestamp in the poll result data
# --DECODE_STRUCT_TIMESTAMP_C_TYPE - the C data type for the timestamp in the decoded data struct
# --DECODE_STRUCT_TIMESTAMP_RESOLUTION_US - the resolution of the timestamp in the decoded data struct

def process_dev_types(json_paths, dev_type_header_path, dev_poll_header_path, gen_options):
    # Remove any leading or trailing square brackets
    json_paths = json_paths.strip()
    if json_paths[0] == '[':
        json_paths = json_paths[1:]
    if json_paths[-1] == ']':
        json_paths = json_paths[:-1]
    # Split the input string into a list of file paths
    json_files = json_paths.split(',')
    decodeGenerator = DecodeGenerator(gen_options)

    # Add MicroPython generator if requested
    micropython_generator = None
    if gen_options.get("generate_micropython", False):
        micropython_generator = MicroPythonGenerator(gen_options, decodeGenerator)

    dev_ident_json = {'devTypes': {}}
    for json_file_path in json_files:
        if json_file_path.strip() == "":
            continue
        with open(json_file_path.strip(), 'r') as json_file:
            file_json = json.load(json_file)
            # Merge device types from multiple JSON files
            dev_ident_json['devTypes'].update(file_json.get('devTypes', {}))

    # Address range
    min_addr_array_index = 0
    max_addr_array_index = 0x7f
    min_valid_i2c_addr = 0x04
    max_valid_i2c_addr = 0x77

    # Address indexes
    addr_index_to_dev_record = {}

    # Scan priorities
    NUM_PRIORITY_LEVELS = 3
    addr_scan_priority_sets = [set() for _ in range(NUM_PRIORITY_LEVELS)]
    scan_priority_values = {
        "high": 0,
        "medium": 1,
        "low": 2
    }

    # Iterate device type records
    dev_record_index = 0
    for dev_type in dev_ident_json['devTypes'].values():
        # print(dev_type["addresses"])

        # Extract list of addresses from addresses field
        # Addresses field can be of the form 0xXX or 0xXX-0xYY or 0xXX,0xYY,0xZZ
        addr_list = []
        addresses = dev_type.get("addresses", "").strip()
        if addresses != "":
            addr_range_list = addresses.split(",")
            for addr_range in addr_range_list:
                addr_range = addr_range.strip()
                if re.match(r'^0x[0-9a-fA-F]{2}-0x[0-9a-fA-F]{2}$', addr_range):
                    addr_range_parts = addr_range.split("-")
                    addr_start = int(addr_range_parts[0], 16)
                    addr_end = int(addr_range_parts[1], 16)
                    for addr in range(addr_start, addr_end+1):
                        addr_list.append(addr)
                elif re.match(r'^0x[0-9a-fA-F]{2}$', addr_range):
                    addr_list.append(int(addr_range, 16))
                else:
                    print(f"Invalid address range {addr_range}")
                    sys.exit(1)

        # Debug
        addr_list_hex = ",".join([f'0x{addr:02x}' for addr in addr_list])
        print(f"Record {dev_record_index} Addresses {addr_range} Addr List {addr_list_hex}")

        # Scan priority
        if "scanPriority" in dev_type:
            scan_priority = dev_type.get("scanPriority", 0)

            # If scan_priority is an array then set scan priority for primary address to first
            # element and for all other addresses to the second element
            priority_value = scan_priority-1 if isinstance(scan_priority, int) else scan_priority_values[scan_priority] if isinstance(scan_priority, str) else NUM_PRIORITY_LEVELS-1
            if priority_value < 0:
                priority_value = 0
            if priority_value > NUM_PRIORITY_LEVELS-1:
                priority_value = NUM_PRIORITY_LEVELS-1
            if priority_value == 0:
                addr_scan_priority_sets[0].add(addr_list[0])
                for addr in addr_list[1:]:
                    addr_scan_priority_sets[1].add(addr)
            else:
                for addr in addr_list:
                    addr_scan_priority_sets[priority_value].add(addr)

        # Generate a dictionary with all addresses referring to a record index
        for addr in addr_list:
            if addr in addr_index_to_dev_record:
                addr_index_to_dev_record[addr].append(dev_record_index)
            else:
                addr_index_to_dev_record[addr] = [dev_record_index]

        # Bump index
        dev_record_index += 1

    # Generate array covering all addresses from 0x00 to 0x77
    max_count_of_dev_types_for_addr = 0
    addr_index_to_dev_array = []
    for addr in range(min_addr_array_index, max_addr_array_index+1):
        if addr in addr_index_to_dev_record:
            addr_index_to_dev_array.append(addr_index_to_dev_record[addr])
            if len(addr_index_to_dev_record[addr]) > max_count_of_dev_types_for_addr:
                max_count_of_dev_types_for_addr = len(addr_index_to_dev_record[addr])
        else:
            addr_index_to_dev_array.append([])

    # Debug
    print(addr_index_to_dev_record)

    # Generate poll records header file
    with open(dev_poll_header_path, 'w') as header_file:
        # Write header
        header_file.write('#pragma once\n')
        header_file.write('#include <stdint.h>\n')

        # Generate structs for the device type records if enabled
        if gen_options.get("gen_decode", False):
            struct_defs = decodeGenerator.get_struct_defs(dev_ident_json['devTypes'])
            for struct_def in struct_defs:
                header_file.write(struct_def)
                header_file.write("\n")

    # Generate dev type header file
    with open(dev_type_header_path, 'w') as header_file:

        # Write header
        header_file.write('#pragma once\n')
        header_file.write('#include <stdint.h>\n')
        header_file.write('#include "DevicePollRecords_generated.h"\n')
        header_file.write('#include "RaftUtils.h"\n')
        header_file.write('using namespace Raft;\n\n')

        # Generate the DeviceTypeRecord array
        header_file.write('static DeviceTypeRecord baseDevTypeRecords[] =\n')
        header_file.write('{\n')

        # Iterate records
        dev_record_index = 0
        for dev_type_key, dev_type in dev_ident_json['devTypes'].items():

            # Get the poll data size
            poll_data_size_bytes = decodeGenerator.poll_data_bytes(dev_type)

            # Check the size matches the JSON value
            poll_size_value_in_json = dev_type.get("devInfoJson", {}).get("resp", {}).get("b", 0)
            if poll_data_size_bytes != poll_size_value_in_json:
                print(f"Poll data size mismatch for {dev_type_key} JSON {poll_size_value_in_json} Calculated {poll_data_size_bytes}")
                sys.exit(1)

            # Convert JSON parts to strings without unnecessary spaces
            polling_config_json_str = json.dumps(dev_type.get("pollInfo",{}), separators=(',', ':'))
            dev_info_json_str = json.dumps(dev_type.get("devInfoJson",{}), separators=(',', ':'))
            header_file.write('    {\n')
            header_file.write(f'        R"({dev_type.get("deviceType","")})",\n')
            header_file.write(f'        R"({dev_type.get("addresses","")})",\n')
            header_file.write(f'        R"({dev_type.get("detectionValues","")})",\n')
            header_file.write(f'        R"({dev_type.get("initValues","")})",\n')
            header_file.write(f'        R"({polling_config_json_str})",\n')
            header_file.write(f'        {str(poll_data_size_bytes)},\n')
            if gen_options.get("inc_dev_info_json", False):
                header_file.write(f'        R"({dev_info_json_str})"')
            else:
                header_file.write(f'        nullptr')

            # Check if gen_decode is set
            if gen_options.get("gen_decode", False):
                header_file.write(f',\n        {decodeGenerator.decode_fn(dev_type)}')

            header_file.write('\n    },\n')
            dev_record_index += 1

        header_file.write('};\n\n')

        # Write constants for the min and max values of array index
        header_file.write(f'static const uint32_t BASE_DEV_INDEX_BY_ARRAY_MIN_ADDR = 0;\n')
        header_file.write(f'static const uint32_t BASE_DEV_INDEX_BY_ARRAY_MAX_ADDR = 0x77;\n\n')

        # Generate the count of device types for each address
        header_file.write(f'static const uint8_t baseDevTypeCountByAddr[] =\n')
        header_file.write('{\n    ')
        for addr_index in addr_index_to_dev_array:
            header_file.write(f'{len(addr_index)},')
        header_file.write('\n};\n\n')

        # For every non-zero length array, generate a C variable specific to that array
        for addr in range(min_addr_array_index, max_addr_array_index+1):
            if len(addr_index_to_dev_array[addr]) > 0:
                header_file.write(f'static uint16_t baseDevTypeIndexByAddr_0x{addr:02x}[] = ')
                header_file.write('{')
                if len(addr_index_to_dev_array[addr]) > 0:
                    header_file.write(f'{addr_index_to_dev_array[addr][0]}')
                    for i in range(1, len(addr_index_to_dev_array[addr])):
                        header_file.write(f', {addr_index_to_dev_array[addr][i]}')
                header_file.write('};\n')

        # Generate the DeviceTypeRecord index by address
        header_file.write(f'\nstatic uint16_t* baseDevTypeIndexByAddr[] =\n')
        header_file.write('{\n')

        # Iterate address array
        for addr in range(min_addr_array_index, max_addr_array_index+1):
            if len(addr_index_to_dev_array[addr]) == 0:
                header_file.write('    nullptr,\n')
            else:
                header_file.write(f'    baseDevTypeIndexByAddr_0x{addr:02x},\n')

        header_file.write('};\n')

        # Generate the scan priority lists
        addr_scan_priority_lists = []
        for i in range(NUM_PRIORITY_LEVELS):
            if i == 0:
                addr_scan_priority_lists.append(sorted(addr_scan_priority_sets[i]))
            else:
                if i == NUM_PRIORITY_LEVELS-1:
                    addr_scan_priority_sets[i] = set(range(min_valid_i2c_addr, max_valid_i2c_addr+1))
                for j in range(i):
                    addr_scan_priority_sets[i] = addr_scan_priority_sets[i] - addr_scan_priority_sets[j]
                addr_scan_priority_lists.append(sorted(addr_scan_priority_sets[i]))

        # Write out the priority scan lists
        for i in range(NUM_PRIORITY_LEVELS):
            header_file.write(f'\n\nstatic const uint8_t scanPriority{i}[] =\n')
            header_file.write('{\n    ')
            for addr in addr_scan_priority_lists[i]:
                header_file.write(f'0x{addr:02x},')
            header_file.write('\n};\n')

        # Write a record for scan lists
        header_file.write('\nstatic const uint8_t* scanPriorityLists[] =\n')
        header_file.write('{\n')
        for i in range(NUM_PRIORITY_LEVELS):
            header_file.write(f'    scanPriority{i},\n')
        header_file.write('};\n')

        # Write out the lengths of the priority scan lists
        header_file.write('\nstatic const uint8_t scanPriorityListLengths[] =\n')
        header_file.write('{\n')
        for i in range(NUM_PRIORITY_LEVELS):
            header_file.write(f'    {len(addr_scan_priority_lists[i])},\n')
        header_file.write('};\n')

        # Write out the number of priority scan lists
        header_file.write(f'\nstatic const uint8_t numScanPriorityLists = {NUM_PRIORITY_LEVELS};\n')

    # Generate MicroPython files if requested
    if micropython_generator:
        generate_micropython_files(dev_ident_json['devTypes'], micropython_generator, gen_options)

def generate_micropython_files(dev_type_records, micropython_generator, gen_options):
    """Generate all MicroPython support files in separate function"""
    
    # Generate QSTR definitions
    qstr_header_path = gen_options.get("mp_qstr_header", "")
    if qstr_header_path:
        micropython_generator.generate_qstr_header_file(dev_type_records, qstr_header_path)
    
    # Generate decoder header
    decoder_header_path = gen_options.get("mp_decoder_header", "")
    if decoder_header_path:
        micropython_generator.generate_micropython_header(dev_type_records, decoder_header_path)
    
    # Generate decoder source
    decoder_source_path = gen_options.get("mp_decoder_source", "")
    if decoder_source_path:
        micropython_generator.generate_micropython_source(dev_type_records, decoder_source_path)

if __name__ == "__main__":
    argparse = argparse.ArgumentParser()
    argparse.add_argument("json_path", help="Path to the JSON file with the device types")
    argparse.add_argument("dev_type_header_path", help="Path to the device type records header file to generate")
    argparse.add_argument("dev_poll_header_path", help="Path to the device poll records header file to generate")
    # Arg for generation of c++ code to decode poll results
    argparse.add_argument("--gendecode", help="Generate C++ code to decode poll results", action="store_false")
    # Arg for inclusion of device info JSON in the header file
    argparse.add_argument("--incdevjson", help="Include device info JSON in the header file", action="store_false")
    # Arg for size of timestamp info in poll data
    argparse.add_argument("--pollresptsbytes", help="Poll response timestamp size (bytes)", type=int, default=2)
    # Arg for poll data timestamp resolution in us
    argparse.add_argument("--pollresptsresus", help="Poll response timestamp resolution in us", type=int, default=1000)
    # Arg for decoded timestamp C data type in extracted data struct
    argparse.add_argument("--decodestructtsctype", help="Decoded struct timestamp C data type", default="uint32_t") 
    # Arg for decoded timestamp C resolution in us
    argparse.add_argument("--decodestructtsresus", help="Decoded struct timestamp resolution in us", type=int, default=1000)
    # Arg for decoded timestamp variable name
    argparse.add_argument("--decodestructtsvar", help="Decoded struct timestamp variable name", default="")
    
    # MicroPython generation arguments
    argparse.add_argument("--gen-micropython", help="Generate MicroPython support files", action="store_true")
    argparse.add_argument("--mp-qstr-header", help="Path to generated QSTR header file", default="")
    argparse.add_argument("--mp-decoder-header", help="Path to generated MicroPython decoder header", default="")
    argparse.add_argument("--mp-decoder-source", help="Path to generated MicroPython decoder source", default="")
    argparse.add_argument("--mp-module-name", help="MicroPython module name", default="device")
    argparse.add_argument("--mp-include-metadata", help="Include device metadata in MicroPython dicts", action="store_true", default=True)
    argparse.add_argument("--mp-include-units", help="Include field units in MicroPython dicts", action="store_true")
    argparse.add_argument("--mp-include-ranges", help="Include field ranges in MicroPython dicts", action="store_true")
    args = argparse.parse_args()
    gen_options = {
        "gen_decode": args.gendecode,
        "inc_dev_info_json": args.incdevjson,
        "POLL_RESULT_TIMESTAMP_SIZE": args.pollresptsbytes,
        "POLL_RESULT_RESOLUTION_US": args.pollresptsresus,
        "DECODE_STRUCT_TIMESTAMP_C_TYPE": args.decodestructtsctype,
        "DECODE_STRUCT_TIMESTAMP_RESOLUTION_US": args.decodestructtsresus,
        "struct_time_var_name": args.decodestructtsvar,
        # MicroPython options
        "generate_micropython": args.gen_micropython,
        "mp_qstr_header": args.mp_qstr_header,
        "mp_decoder_header": args.mp_decoder_header,
        "mp_decoder_source": args.mp_decoder_source,
        "micropython_module_name": args.mp_module_name,
        "mp_include_metadata": args.mp_include_metadata,
        "mp_include_units": args.mp_include_units,
        "mp_include_ranges": args.mp_include_ranges
    }
    process_dev_types(args.json_path, args.dev_type_header_path, args.dev_poll_header_path, gen_options)
    sys.exit(0)
