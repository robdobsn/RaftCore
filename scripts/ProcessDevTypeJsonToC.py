import json
import sys
import re
import argparse

from DecodeGenerator import DecodeGenerator
import DeviceTypeGrammar
import AssembleDeviceTypes
from PseudocodeHandler import PseudocodeHandler
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

# Records whose devInfoJson "type" has always differed from the device type key. Clients may match on these
# values, so they are kept as they are (the device type key is available to clients as deviceTypeKey)
DEV_INFO_TYPE_DIFFERS_FROM_KEY = {
    "AdafruitGamepad": "Gamepad",
    "RoboticalThermocoupleAmp": "Thermocouple",
}

def check_and_normalise_device_type_keys(dev_types, json_file_path):
    """Check the device type key of each record and fill in its deviceType field

    The key of each record in devTypes is the device type key - the record's identifier. A record may omit
    "deviceType"; if present it must equal the key. Afterwards every record's "deviceType" is the key, so the
    rest of the generator (and the MicroPython generator) gets the key from either.
    devInfoJson "type" is a copy of the key kept for clients; a new mismatch gives a warning
    """
    for dev_type_key, dev_type in dev_types.items():
        declared_type = dev_type.get("deviceType")
        if (declared_type is not None) and (declared_type != dev_type_key):
            print(f"ERROR: {json_file_path}: record \"{dev_type_key}\" has deviceType \"{declared_type}\" - "
                  f"deviceType must equal the record's key (or be omitted)", file=sys.stderr)
            sys.exit(1)
        dev_type["deviceType"] = dev_type_key
        dev_info = dev_type.get("devInfoJson")
        if isinstance(dev_info, dict) and ("type" in dev_info) and (dev_info["type"] != dev_type_key):
            if DEV_INFO_TYPE_DIFFERS_FROM_KEY.get(dev_type_key) != dev_info["type"]:
                print(f"WARNING: {json_file_path}: record \"{dev_type_key}\" has devInfoJson type \"{dev_info['type']}\" "
                      f"which differs from its key")

SCAN_PRIORITY_NONE = 0xff
SCAN_PRIORITY_NAMES = {"high": 0, "medium": 1, "low": 2}
NUM_SCAN_PRIORITY_LEVELS = 3
FLAG_PROBE_ONLY = 0x01
FLAG_HAS_HW_TYPE_ID = 0x02
FLAG_NO_SCAN_WHILE_POLLED = 0x04

POLICY_ENABLED = 0
POLICY_DISABLED = 1
POLICY_DISABLED_CLAIM = 2

INIT_VALUE_NAME_RE = re.compile(r"\binit\.([A-Za-z_][A-Za-z0-9_]*)")


def init_values(dev_type, dev_type_key):
    """A record's init values (initCode, devdocs/computed-init-values.md): (names, C++ function) or (None, None) if it
    has none. Errors (exit): {name} value slots or {?name} guards in initValues with no initCode, or one whose value
    initCode doesn't assign. The function computes the values from the bytes read by the init steps before the first
    step with a slot or guard"""
    compiled = DeviceTypeGrammar.compile(dev_type.get("initValues", ""), "init")
    slot_names = []
    read_before_slots = 0
    seen_slot = False
    for step in compiled.steps:
        if step.slots or step.guard:
            seen_slot = True
            slot_names += [name for _, name in step.slots] + ([step.guard] if step.guard else [])
        elif not seen_slot:
            read_before_slots += step.read
    init_code = dev_type.get("initCode")
    code = (init_code.get("c", "") if isinstance(init_code, dict) else (init_code or "")).strip()
    if not code:
        if slot_names:
            print(f"ERROR: record \"{dev_type_key}\" initValues has value slots or guards ({', '.join(sorted(set(slot_names)))}) but "
                  f"no initCode to compute them", file=sys.stderr)
            sys.exit(1)
        return None, None
    names = list(dict.fromkeys(INIT_VALUE_NAME_RE.findall(code)))
    missing = [name for name in dict.fromkeys(slot_names) if name not in names]
    if missing:
        print(f"ERROR: record \"{dev_type_key}\" initCode doesn't assign init.{', init.'.join(missing)} (used in "
              f"initValues)", file=sys.stderr)
        sys.exit(1)
    if not slot_names:
        print(f"WARNING: record \"{dev_type_key}\" has initCode but initValues has no value slots or guards to use it")
    for problem in PseudocodeHandler().check_code(code, [], code_kind="init"):
        print(f"INIT CODE WARNING: record \"{dev_type_key}\" initCode: {problem}")
    handler = PseudocodeHandler()
    body = handler.generate_cpp_code(list(handler.lexer(code)),
                                     {rf"^init\.{name}$": f"pValues[{idx}]" for idx, name in enumerate(names)})
    body = "\n".join("            " + line for line in body.strip().split("\n"))
    fn = ("[](const uint8_t* buf, uint32_t readBufLen, int32_t* pValues, uint32_t numValues) -> bool {\n"
          f"            if ((readBufLen < {read_before_slots}) || (numValues < {len(names)})) return false;\n"
          f"{body}\n"
          "            return true;\n"
          "        }")
    return names, fn


def record_initial_policy(dev_type, dev_type_key):
    """A record's initial policy from "disable": absent or false = enabled, true = disabled, "claim" = disabled with
    the address still claimed (reserved, and the identification hook not run there)"""
    value = dev_type.get("disable", False)
    if value is False:
        return POLICY_ENABLED
    if value is True:
        return POLICY_DISABLED
    if value == "claim":
        return POLICY_DISABLED_CLAIM
    print(f"ERROR: record \"{dev_type_key}\" disable must be true, false or \"claim\"", file=sys.stderr)
    sys.exit(1)

def record_ident_fields(dev_type, dev_type_key):
    """Identification and scanning fields of a record, as emitted in its DeviceTypeRecord:
    (scanPriority, flags, identPriority, hwTypeId)

    scanPriority: "high"/"medium"/"low" or 1-3 gives 0-2 (clamped); absent gives SCAN_PRIORITY_NONE
    flags: FLAG_PROBE_ONLY for "excludeFromAddrMap" or "probeOnly"; FLAG_HAS_HW_TYPE_ID if "deviceTypeId" is set;
           FLAG_NO_SCAN_WHILE_POLLED for "scanWhilePolled": false
    identPriority: -128 to 127, default 0
    hwTypeId: "deviceTypeId" as a number or a string such as "0x0084", 0 to 0xffff
    """
    scan_priority = SCAN_PRIORITY_NONE
    if "scanPriority" in dev_type:
        value = dev_type["scanPriority"]
        if isinstance(value, bool):
            value = None
        if isinstance(value, int):
            scan_priority = min(max(value - 1, 0), NUM_SCAN_PRIORITY_LEVELS - 1)
        elif isinstance(value, str) and value in SCAN_PRIORITY_NAMES:
            scan_priority = SCAN_PRIORITY_NAMES[value]
        else:
            # Matches the address map built before records carried this field: an unrecognised value is low
            scan_priority = NUM_SCAN_PRIORITY_LEVELS - 1
    flags = 0
    if dev_type.get("excludeFromAddrMap", False) or dev_type.get("probeOnly", False):
        flags |= FLAG_PROBE_ONLY
    scan_while_polled = dev_type.get("scanWhilePolled", True)
    if not isinstance(scan_while_polled, bool):
        print(f"ERROR: record \"{dev_type_key}\" scanWhilePolled must be true or false", file=sys.stderr)
        sys.exit(1)
    if not scan_while_polled:
        flags |= FLAG_NO_SCAN_WHILE_POLLED
    hw_type_id = 0
    if "deviceTypeId" in dev_type:
        value = dev_type["deviceTypeId"]
        try:
            hw_type_id = int(value, 0) if isinstance(value, str) else int(value)
        except (TypeError, ValueError):
            print(f"ERROR: record \"{dev_type_key}\" has an invalid deviceTypeId {value!r}", file=sys.stderr)
            sys.exit(1)
        if not 0 <= hw_type_id <= 0xffff:
            print(f"ERROR: record \"{dev_type_key}\" deviceTypeId {value!r} is outside 0-0xffff", file=sys.stderr)
            sys.exit(1)
        flags |= FLAG_HAS_HW_TYPE_ID
    ident_priority = dev_type.get("identPriority", 0)
    if not isinstance(ident_priority, int) or isinstance(ident_priority, bool) or not -128 <= ident_priority <= 127:
        print(f"ERROR: record \"{dev_type_key}\" identPriority must be an integer from -128 to 127", file=sys.stderr)
        sys.exit(1)
    return scan_priority, flags, ident_priority, hw_type_id


def duplicate_hw_type_id_warnings(dev_types):
    """Warnings for hardware type IDs (deviceTypeId) used by more than one record

    Lookups by hardware type ID (getRecordByHwTypeId) return only one of them - the first in catalogue order
    """
    keys_by_id = {}
    for dev_type_key, dev_type in dev_types.items():
        if "deviceTypeId" not in dev_type:
            continue
        value = dev_type["deviceTypeId"]
        try:
            hw_type_id = int(value, 0) if isinstance(value, str) else int(value)
        except (TypeError, ValueError):
            continue
        keys_by_id.setdefault(hw_type_id, []).append(dev_type_key)
    return [f"WARNING: deviceTypeId 0x{hw_type_id:04x} is used by {', '.join(keys)} - lookups by hardware type ID "
            f"return {keys[0]}" for hw_type_id, keys in sorted(keys_by_id.items()) if len(keys) > 1]

def report_grammar_issues(dev_type, dev_type_key, decode_generator=None):
    """Print the grammar's errors and warnings for a record's command strings. Returns the number of errors"""
    num_errors = 0
    for field, result in DeviceTypeGrammar.check_record(dev_type):
        source = dev_type.get("detectionValues", "") if field == "detection" else (
            dev_type.get("initValues", "") if field == "init" else dev_type.get("pollInfo", {}).get("c", ""))
        for code, pos in result.issues:
            kind = "ERROR" if DeviceTypeGrammar.is_error(code) else "WARNING"
            num_errors += 1 if kind == "ERROR" else 0
            print(f"GRAMMAR {kind}: record \"{dev_type_key}\" {DeviceTypeGrammar.FIELD_NAMES[field]}: {code} at {pos} "
                  f"in \"{source}\"")
        # The poll size the firmware computes from the steps must equal the size this generator works out
        if field == "poll" and decode_generator is not None and not dev_type.get("pollInfo", {}).get("ext", 0):
            grammar_size = sum(step.read for step in result.steps)
            try:
                generator_size = decode_generator._get_polling_config_result_len_bytes(source)
            except ValueError:
                generator_size = None
            if generator_size is not None and grammar_size != generator_size:
                print(f"GRAMMAR WARNING: record \"{dev_type_key}\" pollInfo.c reads {grammar_size} bytes in the firmware "
                      f"but the generator sizes it as {generator_size}")
    return num_errors

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
        # A catalogue file ({"devTypes": {...}}) or a folder of record files (<Key>.json, in key order) - see
        # AssembleDeviceTypes.py
        try:
            file_records, _ = AssembleDeviceTypes.load_catalogue(json_file_path.strip(), record_file=False)
        except AssembleDeviceTypes.AssembleError as err:
            print(f"ERROR: {err}", file=sys.stderr)
            sys.exit(1)
        file_dev_types = file_records
        # Policy-only entries ({"disable": true} or {"disable": "claim"}, no record body) set the policy of a
        # record from an earlier file and are not records themselves (D23)
        for dev_type_key in list(file_dev_types.keys()):
            entry = file_dev_types[dev_type_key]
            if isinstance(entry, dict) and "disable" in entry and set(entry.keys()) <= {"disable", "deviceType", "_notes"}:
                del file_dev_types[dev_type_key]
                if dev_type_key in dev_ident_json['devTypes']:
                    dev_ident_json['devTypes'][dev_type_key]["disable"] = entry["disable"]
                else:
                    print(f"WARNING: {json_file_path.strip()}: \"{dev_type_key}\" sets a policy but no earlier file "
                          f"has a record with that key - ignored")
        check_and_normalise_device_type_keys(file_dev_types, json_file_path.strip())
        # Merge device types from multiple JSON files (a later file's record replaces an earlier one's)
        for dev_type_key in file_dev_types:
            if dev_type_key in dev_ident_json['devTypes']:
                print(f"WARNING: {json_file_path.strip()}: record \"{dev_type_key}\" replaces a record with the "
                      f"same key in an earlier file")
        dev_ident_json['devTypes'].update(file_dev_types)

    # Highest address the firmware identifies by
    max_valid_i2c_addr = 0x77

    # Records (in order) that identify a device by each address - for the shadowing report
    addr_to_record_keys = {}

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

            # Addresses above the 7-bit range used by the firmware's address index can't be identified -
            # fail rather than drop them silently (C12)
            bad_addrs = [addr for addr in addr_list if addr > max_valid_i2c_addr]
            if len(bad_addrs) > 0:
                bad_addrs_hex = ",".join([f'0x{addr:02x}' for addr in bad_addrs])
                print(f"ERROR: record \"{dev_type['deviceType']}\" has addresses {bad_addrs_hex} above "
                      f"0x{max_valid_i2c_addr:02x}, which can't be identified", file=sys.stderr)
                sys.exit(1)

        # Debug
        addr_list_hex = ",".join([f'0x{addr:02x}' for addr in addr_list])
        print(f"Record {dev_record_index} Addresses {addresses} Addr List {addr_list_hex}")

        # Identification and scanning fields (validated here; the firmware builds its address map and scan lists
        # from them at runtime). A probe-only record (e.g. Robotical RSAOs, identified by an identification
        # hook rather than by bus address) isn't identified by address
        _, flags, _, _ = record_ident_fields(dev_type, dev_type["deviceType"])

        # Command strings: report problems found by the grammar the firmware uses (D19: reported, not yet
        # rejected - an error means the firmware won't use that detection string, or drops that step)
        report_grammar_issues(dev_type, dev_type["deviceType"], decodeGenerator)

        # The poll bus speed is a choice for the board (DevMan "typeSettings"), no longer part of a record
        for bus_key in ("busHz", "h", "busHzSlots", "hSlots"):
            if bus_key in dev_type.get("pollInfo", {}):
                print(f"WARNING: record \"{dev_type['deviceType']}\" pollInfo.{bus_key} is no longer used - set the "
                      f"poll bus speed in the SysType's DevMan typeSettings")

        # Decode code is compiled into the firmware: report anything outside the decode language (a record from a git
        # source with such code is rejected when the records are assembled - AssembleDeviceTypes.py)
        custom_decode = dev_type.get("devInfoJson", {}).get("resp", {}).get("c")
        if isinstance(custom_decode, dict) and custom_decode.get("c"):
            attr_names = [attr.get("n", "") for attr in dev_type["devInfoJson"]["resp"].get("a", [])]
            for problem in PseudocodeHandler().check_code(custom_decode["c"], attr_names):
                print(f"DECODE CODE WARNING: record \"{dev_type['deviceType']}\" resp.c: {problem}")
        if not (flags & FLAG_PROBE_ONLY):
            for addr in addr_list:
                addr_to_record_keys.setdefault(addr, []).append(dev_type["deviceType"])

        # Bump index
        dev_record_index += 1

    # Shadowing report: a record whose detection compares nothing matches any device at its address, so records
    # tried after it there are never tried. The firmware tries records at an address in order of descending
    # identPriority, then descending detection strength (the bits compared), then record order - so this happens
    # only when such a record has a higher identPriority, or another record also compares nothing (a record whose
    # detection has errors never matches, so it isn't shadowed)
    dev_types_by_key = dev_ident_json['devTypes']
    record_order = {key: i for i, key in enumerate(dev_types_by_key.keys())}

    def detection_strength(key):
        return DeviceTypeGrammar.detection_strength(
            DeviceTypeGrammar.compile(dev_types_by_key[key].get("detectionValues", ""), "detection"))

    for addr, keys in sorted(addr_to_record_keys.items()):
        keys = sorted(keys, key=lambda k: (-dev_types_by_key[k].get("identPriority", 0), -detection_strength(k),
                                           record_order[k]))
        for i, key in enumerate(keys[:-1]):
            shadowed = [k for k in keys[i+1:] if detection_strength(k) >= 0]
            if (detection_strength(key) == 0) and shadowed:
                print(f"WARNING: record \"{key}\" has no detection values, so at address 0x{addr:02x} it is matched "
                      f"before {', '.join(shadowed)}")
                break

    # Generate poll records header file
    with open(dev_poll_header_path, 'w') as header_file:
        # Write header
        header_file.write('#pragma once\n')
        header_file.write('#include <stdint.h>\n')
        header_file.write('#include <cstddef>\n')
        header_file.write('#include "DeviceTypeRecord.h"\n\n')

        # Generate structs for the device type records if enabled
        if gen_options.get("gen_decode", False):
            struct_defs = decodeGenerator.get_struct_defs(dev_ident_json['devTypes'])
            for struct_def in struct_defs:
                header_file.write(struct_def)
                header_file.write("\n")

            # Generate AttrFieldDesc tables for each struct
            field_desc_defs = decodeGenerator.get_field_desc_defs(dev_ident_json['devTypes'])
            for field_desc_def in field_desc_defs:
                header_file.write(field_desc_def)
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
        header_file.write('static const DeviceTypeRecord baseDevTypeRecords[] =\n')
        header_file.write('{\n')

        # A hardware type ID should name one record (N2)
        for warning in duplicate_hw_type_id_warnings(dev_ident_json['devTypes']):
            print(warning, file=sys.stderr)

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
            header_file.write(f'        R"JSON({dev_type_key})JSON",\n')
            header_file.write(f'        R"JSON({dev_type.get("addresses","")})JSON",\n')
            header_file.write(f'        R"JSON({dev_type.get("detectionValues","")})JSON",\n')
            header_file.write(f'        R"JSON({dev_type.get("initValues","")})JSON",\n')
            header_file.write(f'        R"JSON({polling_config_json_str})JSON",\n')
            header_file.write(f'        {str(poll_data_size_bytes)},\n')
            if gen_options.get("inc_dev_info_json", False):
                header_file.write(f'        R"JSON({dev_info_json_str})JSON"')
            else:
                header_file.write(f'        nullptr')

            # Check if gen_decode is set
            if gen_options.get("gen_decode", False):
                header_file.write(f',\n        {decodeGenerator.decode_fn(dev_type, dev_type_key)}')
                header_file.write(f',\n        {decodeGenerator.field_desc_ref(dev_type, dev_type_key)}')
            else:
                header_file.write(',\n        nullptr, nullptr, 0, 0')

            # Identification and scanning fields: scanPriority, flags, identPriority, hwTypeId
            scan_priority, flags, ident_priority, hw_type_id = record_ident_fields(dev_type, dev_type_key)
            header_file.write(f',\n        {scan_priority}, {flags}, {ident_priority}, 0x{hw_type_id:04x}, '
                              f'{record_initial_policy(dev_type, dev_type_key)}')

            # Init values (initCode): the function and the value names
            init_names, init_fn = init_values(dev_type, dev_type_key)
            if init_fn:
                if gen_options.get("gen_decode", False):
                    header_file.write(f',\n        {init_fn},\n        R"JSON({",".join(init_names)})JSON"')
                else:
                    print(f"WARNING: record \"{dev_type_key}\" has initCode, which isn't generated without decode "
                          f"generation - its init will fail")

            header_file.write('\n    },\n')
            dev_record_index += 1

        header_file.write('};\n\n')

        # Generate the record indexes sorted by device type key (byte order, as strcmp) for binary search
        dev_type_keys = list(dev_ident_json['devTypes'].keys())
        sorted_key_indexes = sorted(range(len(dev_type_keys)), key=lambda i: dev_type_keys[i].encode('utf-8'))
        header_file.write('\nstatic const uint16_t baseDevTypeIndexByKey[] =\n')
        header_file.write('{\n    ')
        for record_index in sorted_key_indexes:
            header_file.write(f'{record_index},')
        header_file.write('\n};\n')


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
