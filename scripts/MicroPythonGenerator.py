import re

class MicroPythonGenerator:
    """
    Generator for MicroPython device support code that is completely agnostic
    to the contents of DevTypes.json files. Dynamically discovers device types,
    field names, and generates appropriate MicroPython bindings.
    """
    
    def __init__(self, generator_options, decode_generator=None):
        """
        Initialize MicroPython generator with options and optional reference to DecodeGenerator
        
        Args:
            generator_options: Dictionary of generation options
            decode_generator: Optional DecodeGenerator instance for shared utilities
        """
        # MicroPython-specific configuration
        self.generate_micropython = generator_options.get("generate_micropython", False)
        self.micropython_module_name = generator_options.get("micropython_module_name", "device")
        self.qstr_prefix = generator_options.get("qstr_prefix", "MP_QSTR_")
        
        # Reference to existing DecodeGenerator for shared utilities
        self.decode_generator = decode_generator
        
        # MicroPython feature flags
        self.include_device_metadata = generator_options.get("mp_include_metadata", True)
        self.include_field_units = generator_options.get("mp_include_units", False)
        self.include_field_ranges = generator_options.get("mp_include_ranges", False)
        
        # Runtime discovered data (populated during processing)
        self.discovered_device_types = set()
        self.discovered_field_names = set()
        self.discovered_units = set()
        self.discovered_metadata_keys = set()
        
        # Standard module QSTRs (only hardcoded items)
        self.standard_module_qstrs = {
            "device", "on_data", "on_status", "get_data", "list", 
            "timestamp", "type", "name", "online"
        }
    
    def discover_all_qstr_candidates(self, dev_type_records):
        """
        Dynamically discover all QSTR candidates from any DevTypes.json structure
        
        Args:
            dev_type_records: Dictionary of device type records from DevTypes.json
            
        Returns:
            Sorted list of unique QSTR candidates
        """
        qstrs = set()
        
        # Add standard module QSTRs
        qstrs.update(self.standard_module_qstrs)
        
        # Process each device type record dynamically
        for dev_type_key, dev_type_record in dev_type_records.items():
            # Discover from device type record itself
            qstrs.update(self._discover_qstrs_from_device_record(dev_type_record))
            
            # Discover from device info JSON
            dev_info_json = dev_type_record.get("devInfoJson", {})
            qstrs.update(self._discover_qstrs_from_device_info(dev_info_json))
        
        return sorted(qstrs)
    
    def _discover_qstrs_from_device_record(self, dev_type_record):
        """Extract QSTRs from top-level device type record"""
        qstrs = set()
        
        # Device type name
        device_type = dev_type_record.get("deviceType", "")
        if device_type:
            qstrs.add(device_type)
            self.discovered_device_types.add(device_type)
        
        # Scan priority if it's a string
        scan_priority = dev_type_record.get("scanPriority", "")
        if isinstance(scan_priority, str) and scan_priority:
            qstrs.add(scan_priority)
            self.discovered_metadata_keys.add("scanPriority")
        
        return qstrs
    
    def _discover_qstrs_from_device_info(self, dev_info_json):
        """Extract QSTRs from device info JSON structure"""
        qstrs = set()
        
        # Top-level device info fields - discover dynamically
        for key, value in dev_info_json.items():
            if key == "resp":
                continue  # Handle response separately
            
            if isinstance(value, str) and value:
                qstrs.add(key)  # The key itself
                self.discovered_metadata_keys.add(key)
                
                # Clean value for use as identifier
                clean_value = self._to_valid_identifier(value)
                if clean_value:
                    qstrs.add(clean_value)
        
        # Response structure fields
        resp_info = dev_info_json.get("resp", {})
        if resp_info:
            # Process all attributes in response
            for attr in resp_info.get("a", []):
                qstrs.update(self._discover_qstrs_from_attribute(attr))
        
        return qstrs
    
    def _discover_qstrs_from_attribute(self, attr):
        """Extract QSTRs from a single attribute definition"""
        qstrs = set()
        
        # Field name (most important)
        field_name = attr.get("n", "")
        if field_name:
            qstrs.add(field_name)
            self.discovered_field_names.add(field_name)
        
        # Unit if including units
        if self.include_field_units:
            unit = attr.get("u", "")
            if unit:
                qstrs.add(unit)
                self.discovered_units.add(unit)
                # Also add unit key for field_name_unit pattern
                if field_name:
                    unit_key = f"{field_name}_unit"
                    qstrs.add(unit_key)
        
        # Output type for metadata
        if self.include_device_metadata:
            output_type = attr.get("o", "")
            if output_type:
                qstrs.add(output_type)
                self.discovered_metadata_keys.add("output_type")
        
        return qstrs
    
    def _to_valid_identifier(self, value):
        """Convert any string to a valid identifier for QSTR"""
        if not value:
            return ""
        
        # Use DecodeGenerator's utility if available
        if self.decode_generator:
            return self.decode_generator.to_valid_c_var_name(value)
        
        # Fallback implementation
        clean = re.sub(r'[^a-zA-Z0-9_]', '_', value)
        if clean and clean[0].isdigit():
            clean = '_' + clean
        return clean.lower()
    
    def generate_qstr_definitions(self, qstrs):
        """
        Generate QSTR definitions for qstrdefsport.h
        
        Args:
            qstrs: List of QSTR candidates
            
        Returns:
            String containing QSTR definitions
        """
        lines = []
        lines.append("// Auto-generated QSTRs for device module")
        lines.append("// Generated from DevTypes.json")
        lines.append("")
        
        for qstr in qstrs:
            # Convert to valid QSTR name
            qstr_name = re.sub(r'[^a-zA-Z0-9_]', '_', qstr)
            lines.append(f"Q({qstr_name})")
        
        return "\n".join(lines)
    
    def generate_qstr_header_file(self, dev_type_records, output_path):
        """
        Generate complete QSTR header file
        
        Args:
            dev_type_records: Dictionary of device type records
            output_path: Path to write the header file
        """
        # Discover all QSTRs
        qstrs = self.discover_all_qstr_candidates(dev_type_records)
        
        # Generate content
        content = self.generate_qstr_definitions(qstrs)
        
        # Write to file
        with open(output_path, 'w') as f:
            f.write("// Auto-generated QSTR definitions for MicroPython device module\n")
            f.write("// Generated from DevTypes.json\n")
            f.write("// DO NOT EDIT - This file is automatically generated\n\n")
            f.write(content)
    
    def generate_micropython_header(self, dev_type_records, output_path):
        """
        Generate MicroPython decoder header file
        
        Args:
            dev_type_records: Dictionary of device type records
            output_path: Path to write the header file
        """
        with open(output_path, 'w') as f:
            f.write('#pragma once\n')
            f.write('#include "py/obj.h"\n\n')
            f.write('// Auto-generated MicroPython decoder declarations\n')
            f.write('// Generated from DevTypes.json\n')
            f.write('// DO NOT EDIT - This file is automatically generated\n\n')
            
            # Generate decoder function declarations for each device type
            for dev_type_key, dev_type_record in dev_type_records.items():
                device_type = dev_type_record.get("deviceType", "")
                if device_type:
                    func_name = f"mp_decode_{device_type}"
                    f.write(f'// Decoder for {device_type}\n')
                    f.write(f'mp_obj_t {func_name}(const uint8_t* pBuf, uint32_t bufLen, uint64_t timestampUs);\n\n')
            
            # Device lookup function
            f.write('// Device decoder lookup function\n')
            f.write('mp_obj_t mp_decode_device_data(const char* deviceType, const uint8_t* pBuf, uint32_t bufLen, uint64_t timestampUs);\n\n')
            
            # Utility functions
            f.write('// Utility functions\n')
            f.write('mp_obj_t mp_create_device_dict_with_timestamp(uint64_t timestampUs);\n')
            f.write('void mp_dict_store_float(mp_obj_t dict, qstr key_qstr, float value);\n')
            f.write('void mp_dict_store_int(mp_obj_t dict, qstr key_qstr, int value);\n')
            f.write('void mp_dict_store_str(mp_obj_t dict, qstr key_qstr, const char* value);\n')
    
    def generate_micropython_source(self, dev_type_records, output_path):
        """
        Generate MicroPython decoder source file
        
        Args:
            dev_type_records: Dictionary of device type records
            output_path: Path to write the source file
        """
        with open(output_path, 'w') as f:
            f.write('#include "DeviceMPDecoders_generated.h"\n')
            f.write('#include "DevicePollRecords_generated.h"\n')
            f.write('#include "py/runtime.h"\n')
            f.write('#include "py/objstr.h"\n')
            f.write('#include <string.h>\n\n')
            f.write('// Auto-generated MicroPython decoder implementations\n')
            f.write('// Generated from DevTypes.json\n')
            f.write('// DO NOT EDIT - This file is automatically generated\n\n')
            
            # Generate C data extraction helper functions first
            self._generate_c_data_helpers(f)
            
            # Generate utility functions
            self._generate_utility_functions(f)
            
            # Generate decoder functions for each device type
            for dev_type_key, dev_type_record in dev_type_records.items():
                self._generate_device_decoder_function(f, dev_type_record)
            
            # Generate lookup table and main decoder function
            self._generate_device_lookup_function(f, dev_type_records)
    
    def _generate_utility_functions(self, f):
        """Generate utility functions for MicroPython dict creation and manipulation"""
        f.write('// Utility functions for MicroPython dict creation\n\n')
        
        # Function to create dict with timestamp
        f.write('mp_obj_t mp_create_device_dict_with_timestamp(uint64_t timestampUs) {\n')
        f.write('    mp_obj_t dict = mp_obj_new_dict(8); // Pre-allocate space for typical device data\n')
        f.write('    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_timestamp), mp_obj_new_int(timestampUs));\n')
        f.write('    return dict;\n')
        f.write('}\n\n')
        
        # Function to store float values
        f.write('void mp_dict_store_float(mp_obj_t dict, qstr key_qstr, float value) {\n')
        f.write('    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(key_qstr), mp_obj_new_float(value));\n')
        f.write('}\n\n')
        
        # Function to store int values
        f.write('void mp_dict_store_int(mp_obj_t dict, qstr key_qstr, int value) {\n')
        f.write('    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(key_qstr), mp_obj_new_int(value));\n')
        f.write('}\n\n')
        
        # Function to store string values
        f.write('void mp_dict_store_str(mp_obj_t dict, qstr key_qstr, const char* value) {\n')
        f.write('    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(key_qstr), mp_obj_new_str(value, strlen(value)));\n')
        f.write('}\n\n')
    
    def _generate_device_decoder_function(self, f, dev_type_record):
        """Generate decoder function for a specific device type"""
        device_type = dev_type_record.get("deviceType", "")
        if not device_type:
            return
            
        dev_info_json = dev_type_record.get("devInfoJson", {})
        resp_info = dev_info_json.get("resp", {})
        attributes = resp_info.get("a", [])
        expected_bytes = resp_info.get("b", 0)
        
        func_name = f"mp_decode_{device_type}"
        f.write(f'// Decoder for {device_type}\n')
        f.write(f'mp_obj_t {func_name}(const uint8_t* pBuf, uint32_t bufLen, uint64_t timestampUs) {{\n')
        f.write(f'    // Expected data length: {expected_bytes} bytes\n')
        f.write(f'    if (bufLen < {expected_bytes}) {{\n')
        f.write('        return mp_const_none;\n')
        f.write('    }\n\n')
        
        f.write('    // Create result dictionary with timestamp\n')
        f.write('    mp_obj_t result = mp_create_device_dict_with_timestamp(timestampUs);\n\n')
        
        # Add device type and name
        device_name = dev_info_json.get("name", device_type)
        f.write(f'    mp_dict_store_str(result, MP_QSTR_type, "{device_type}");\n')
        f.write(f'    mp_dict_store_str(result, MP_QSTR_name, "{device_name}");\n\n')
        
        # Generate extraction code for each attribute
        buffer_offset = 0
        for attr in attributes:
            attr_name = attr.get("n", "")
            attr_type = attr.get("t", "")
            attr_divisor = attr.get("d", 1)
            attr_offset = attr.get("a", 0)  # Additive offset
            output_type = attr.get("o", "int")
            
            if not attr_name or not attr_type:
                continue
                
            # Generate QSTR name for the attribute
            qstr_name = f"MP_QSTR_{self._to_valid_identifier(attr_name)}"
            
            # Get the extraction method and C type
            extraction_info = self._get_extraction_info(attr_type)
            if not extraction_info:
                f.write(f'    // Skipping unsupported type {attr_type} for {attr_name}\n')
                continue
                
            c_type, extraction_method, byte_size = extraction_info
            
            f.write(f'    // Extract {attr_name} ({attr_type})\n')
            f.write(f'    {c_type} {attr_name}_raw = {extraction_method}(pBuf, {buffer_offset});\n')
            
            # Apply divisor and offset if needed
            if attr_divisor != 1 or attr_offset != 0:
                if output_type == "float":
                    # Format divisor as proper float literal
                    divisor_str = f"{float(attr_divisor)}f" if isinstance(attr_divisor, (int, float)) else f"{attr_divisor}f"
                    f.write(f'    float {attr_name}_value = ({attr_name}_raw / {divisor_str})')
                    if attr_offset != 0:
                        offset_str = f"{float(attr_offset)}f" if isinstance(attr_offset, (int, float)) else f"{attr_offset}f"
                        f.write(f' + {offset_str}')
                    f.write(';\n')
                    f.write(f'    mp_dict_store_float(result, {qstr_name}, {attr_name}_value);\n')
                else:
                    f.write(f'    int {attr_name}_value = ({attr_name}_raw / {attr_divisor})')
                    if attr_offset != 0:
                        f.write(f' + {attr_offset}')
                    f.write(';\n')
                    f.write(f'    mp_dict_store_int(result, {qstr_name}, {attr_name}_value);\n')
            else:
                # Direct storage
                if output_type == "float":
                    f.write(f'    mp_dict_store_float(result, {qstr_name}, (float){attr_name}_raw);\n')
                else:
                    f.write(f'    mp_dict_store_int(result, {qstr_name}, (int){attr_name}_raw);\n')
            
            f.write('\n')
            buffer_offset += byte_size
        
        f.write('    return result;\n')
        f.write('}\n\n')
    
    def _get_extraction_info(self, pystruct_type):
        """
        Get extraction information for a Python struct type
        
        Args:
            pystruct_type: Python struct format string (e.g., '<h', 'B', '>f')
            
        Returns:
            Tuple of (c_type, extraction_method, byte_size) or None if unsupported
        """
        # Map Python struct types to pure C extraction methods
        # Using our generated helper functions instead of C++ RaftUtils
        type_map = {
            'b': ('int8_t', 'mp_get_int8', 1),
            'B': ('uint8_t', 'mp_get_uint8', 1),
            '>h': ('int16_t', 'mp_get_be_int16', 2),
            '<h': ('int16_t', 'mp_get_le_int16', 2),
            '>H': ('uint16_t', 'mp_get_be_uint16', 2),
            '<H': ('uint16_t', 'mp_get_le_uint16', 2),
            '>i': ('int32_t', 'mp_get_be_int32', 4),
            '<i': ('int32_t', 'mp_get_le_int32', 4),
            '>I': ('uint32_t', 'mp_get_be_uint32', 4),
            '<I': ('uint32_t', 'mp_get_le_uint32', 4),
            '>l': ('int32_t', 'mp_get_be_int32', 4),
            '<l': ('int32_t', 'mp_get_le_int32', 4),
            '>L': ('uint32_t', 'mp_get_be_uint32', 4),
            '<L': ('uint32_t', 'mp_get_le_uint32', 4),
            '>f': ('float', 'mp_get_be_float32', 4),
            '<f': ('float', 'mp_get_le_float32', 4),
        }
        
        return type_map.get(pystruct_type, None)
    
    def _generate_device_lookup_function(self, f, dev_type_records):
        """Generate the main device decoder lookup function"""
        f.write('// Device decoder lookup function\n')
        f.write('mp_obj_t mp_decode_device_data(const char* deviceType, const uint8_t* pBuf, uint32_t bufLen, uint64_t timestampUs) {\n')
        f.write('    if (!deviceType || !pBuf) {\n')
        f.write('        return mp_const_none;\n')
        f.write('    }\n\n')
        
        # Generate if-else chain for device type lookup
        first = True
        for dev_type_key, dev_type_record in dev_type_records.items():
            device_type = dev_type_record.get("deviceType", "")
            if not device_type:
                continue
                
            func_name = f"mp_decode_{device_type}"
            condition = "if" if first else "else if"
            first = False
            
            f.write(f'    {condition} (strcmp(deviceType, "{device_type}") == 0) {{\n')
            f.write(f'        return {func_name}(pBuf, bufLen, timestampUs);\n')
            f.write('    }\n')
        
        f.write('\n    // Unknown device type\n')
        f.write('    return mp_const_none;\n')
        f.write('}\n')
    
    def _generate_c_data_helpers(self, f):
        """Generate pure C helper functions for data extraction (avoiding C++ RaftUtils)"""
        f.write('// Pure C data extraction helper functions\n')
        f.write('// These replace the C++ RaftUtils functions to keep the code in pure C\n\n')
        
        # Helper function for bounds checking
        f.write('static inline bool mp_check_buffer_bounds(const uint8_t* pBuf, const uint8_t* pEnd, uint32_t offset, uint32_t size) {\n')
        f.write('    return (pBuf && pEnd && (pBuf + offset + size <= pEnd));\n')
        f.write('}\n\n')
        
        # 8-bit extraction
        f.write('static inline int8_t mp_get_int8(const uint8_t* pBuf, uint32_t offset) {\n')
        f.write('    return *((int8_t*)(pBuf + offset));\n')
        f.write('}\n\n')
        
        f.write('static inline uint8_t mp_get_uint8(const uint8_t* pBuf, uint32_t offset) {\n')
        f.write('    return pBuf[offset];\n')
        f.write('}\n\n')
        
        # 16-bit Little Endian extraction
        f.write('static inline int16_t mp_get_le_int16(const uint8_t* pBuf, uint32_t offset) {\n')
        f.write('    return (int16_t)(pBuf[offset] | (pBuf[offset + 1] << 8));\n')
        f.write('}\n\n')
        
        f.write('static inline uint16_t mp_get_le_uint16(const uint8_t* pBuf, uint32_t offset) {\n')
        f.write('    return (uint16_t)(pBuf[offset] | (pBuf[offset + 1] << 8));\n')
        f.write('}\n\n')
        
        # 16-bit Big Endian extraction  
        f.write('static inline int16_t mp_get_be_int16(const uint8_t* pBuf, uint32_t offset) {\n')
        f.write('    return (int16_t)((pBuf[offset] << 8) | pBuf[offset + 1]);\n')
        f.write('}\n\n')
        
        f.write('static inline uint16_t mp_get_be_uint16(const uint8_t* pBuf, uint32_t offset) {\n')
        f.write('    return (uint16_t)((pBuf[offset] << 8) | pBuf[offset + 1]);\n')
        f.write('}\n\n')
        
        # 32-bit Little Endian extraction
        f.write('static inline int32_t mp_get_le_int32(const uint8_t* pBuf, uint32_t offset) {\n')
        f.write('    return (int32_t)(pBuf[offset] | (pBuf[offset + 1] << 8) | (pBuf[offset + 2] << 16) | (pBuf[offset + 3] << 24));\n')
        f.write('}\n\n')
        
        f.write('static inline uint32_t mp_get_le_uint32(const uint8_t* pBuf, uint32_t offset) {\n')
        f.write('    return (uint32_t)(pBuf[offset] | (pBuf[offset + 1] << 8) | (pBuf[offset + 2] << 16) | (pBuf[offset + 3] << 24));\n')
        f.write('}\n\n')
        
        # 32-bit Big Endian extraction
        f.write('static inline int32_t mp_get_be_int32(const uint8_t* pBuf, uint32_t offset) {\n')
        f.write('    return (int32_t)((pBuf[offset] << 24) | (pBuf[offset + 1] << 16) | (pBuf[offset + 2] << 8) | pBuf[offset + 3]);\n')
        f.write('}\n\n')
        
        f.write('static inline uint32_t mp_get_be_uint32(const uint8_t* pBuf, uint32_t offset) {\n')
        f.write('    return (uint32_t)((pBuf[offset] << 24) | (pBuf[offset + 1] << 16) | (pBuf[offset + 2] << 8) | pBuf[offset + 3]);\n')
        f.write('}\n\n')
        
        # Float extraction helpers (assuming IEEE 754)
        f.write('static inline float mp_get_le_float32(const uint8_t* pBuf, uint32_t offset) {\n')
        f.write('    union { uint32_t u; float f; } converter;\n')
        f.write('    converter.u = mp_get_le_uint32(pBuf, offset);\n')
        f.write('    return converter.f;\n')
        f.write('}\n\n')
        
        f.write('static inline float mp_get_be_float32(const uint8_t* pBuf, uint32_t offset) {\n')
        f.write('    union { uint32_t u; float f; } converter;\n')
        f.write('    converter.u = mp_get_be_uint32(pBuf, offset);\n')
        f.write('    return converter.f;\n')
        f.write('}\n\n')
