import os
import subprocess
from GenKconfigDefinesForPIO import process_kconfig_file 
Import('env')

def determine_build_systype(env, current_dir, source_dir):
    # print("--------------- Determining build systype PIDENV: ", env['PIOENV'], " current_dir: ", current_dir, " source_dir: ", source_dir)
    return env['PIOENV']

def generate_systypes_file(env, build_systype, project_dir, current_dir, artifacts_folder):

    # Folder containing SysTypes.json
    systypes_json = os.path.join(project_dir, "systypes", build_systype, 'SysTypes.json')

    # Check folder exists
    if not os.path.exists(systypes_json):
        print(f"!!!!!!!!!!!!!!!!!!!!!!!!!! SysTypes.json not found: {systypes_json}")
        return

    # Template file for generating SysTypeInfoRecs.h
    systypes_template = os.path.join(current_dir, '..', 'components', 'core', 'SysTypes', 'SysTypeInfoRecs.cpp.template')

    # Output file for generated SysTypeInfoRecs.h
    systypes_out = os.path.join(artifacts_folder, 'SysTypeInfoRecs.h')

    if not os.path.exists(artifacts_folder):
        os.makedirs(artifacts_folder)

    command = [
        env['PYTHONEXE'], os.path.join(current_dir, 'GenerateSysTypes.py'),
        systypes_json, systypes_out, '--cpp_template', systypes_template
    ]

    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"!!!!!!!!!!!!!!!!!! Error generating SysTypeInfoRecs.h: {result.stderr}")
        env.Exit(result.returncode)
    else:
        print("------------------ Generated SysTypeInfoRecs.h successfully")

def generate_device_records(env, current_dir, artifacts_folder):
    json_file = os.path.join(current_dir, '..', 'devtypes', 'DeviceTypeRecords.json')
    dev_type_recs_header = os.path.join(artifacts_folder, 'DeviceTypeRecords_generated.h')
    dev_poll_recs_header = os.path.join(artifacts_folder, 'DevicePollRecords_generated.h')

    if not os.path.exists(artifacts_folder):
        os.makedirs(artifacts_folder)

    command = [
        env['PYTHONEXE'], os.path.join(current_dir, 'ProcessDevTypeJsonToC.py'),
        json_file, dev_type_recs_header, dev_poll_recs_header
    ]

    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"!!!!!!!!!!!!!!!!!!!!! Error generating device records: {result.stderr}")
        env.Exit(result.returncode)
    else:
        print("------------------ Generated device records successfully")

def process_kconfig(env, current_dir):
    kconfig_file = os.path.join(env['PROJECT_LIBDEPS_DIR'], env['PIOENV'], "littlefs", 'Kconfig')
    if os.path.exists(kconfig_file):
        defines = process_kconfig_file(kconfig_file)
        for key, value in defines.items():
            if value is not None and value != '"n"':
                key = "CONFIG_" + key
                env.Append(CPPDEFINES=[(key, value)])
                # print(f"----------------- Added define: {key}={value}")
    else:
        print(f"!!!!!!!!!!!!!!!!!!!!!! Kconfig file not found: {kconfig_file}")

env = DefaultEnvironment()

# print(env.Dump())

# Paths
current_dir = os.getcwd()
project_dir = env['PROJECT_DIR']
source_dir = os.path.dirname(current_dir)
build_dir = env['PROJECT_BUILD_DIR']
build_systype = determine_build_systype(env, current_dir, source_dir)
artifacts_folder = os.path.join(build_dir, build_systype, 'raft')

print(f"------------------ RaftCore systype {build_systype} ------------------")
print(f"------------------ RaftCore build folder {build_dir} ------------------")

# env.Append(CPPDEFINES=[("PROJECT_BASENAME", build_systype)])

# Convert SysTypes.json to C header
generate_systypes_file(env, build_systype, project_dir, current_dir, artifacts_folder)

# Generate device records
generate_device_records(env, current_dir, artifacts_folder)
env.Append(CPPPATH=[os.path.abspath(artifacts_folder)])

print("------------------ Generating Kconfig Defines for LittleFS -----------------")
process_kconfig(env, current_dir)

env.Append(
    CCFLAGS=[
        "-Wno-missing-field-initializers"
    ]
)
