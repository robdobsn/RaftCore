# Import('env')
# from os.path import join, realpath

# # Pass flags to the Global environemnt (project `src` files, frameworks)
# global_env = DefaultEnvironment()
# # Get SCONS environment variables
# env_vars = global_env.Dictionary()
# # Get the path to the project root
# project_root = env_vars['PROJECT_DIR']
# # Get the path to the project `src` directory
# src_dir = join(project_root, 'src')
# # Get the path to the project `lib` directory
# lib_dir = join(project_root, 'lib')
# # Get the path to the project CPPDEFINES
# cpp_defines = env_vars['CPPDEFINES']

# print(f"+++++++++++++++++++++++ {env_vars}, {project_root}, {src_dir}, {lib_dir}, {cpp_defines}")
# global_env.Append(CPPDEFINES=[("TEST_GLOBAL", 1)])

# #
# # Private flags
# #
# for item in env.get("CPPDEFINES", []):
#     if isinstance(item, tuple) and item[0] == "HAL":
#         env.Append(CPPPATH=[realpath(join("hal", item[1]))])
#         env.Replace(SRC_FILTER=["+<*>", "-<hal*>", "+<hal/%s>" % item[1]])
#         break

# #
# # Pass flags to the other Project Dependencies (libraries)
# #
# for lb in env.GetLibBuilders():
#     lb.env.Append(CPPDEFINES=[("TEST_LIBDEPS", 1)])
#     if lb.name == "OneWire":
#         lb.env.Append(CPPDEFINES=[("OW_PIN", 13)])


# # Operate with the project environment (files located in the `src` folder)
# Import("projenv")
# # add (prepend) to the beginning of list
# projenv.Prepend(CPPPATH=["some/path"])
# # remove specified flags
# projenv.ProcessUnFlags("-fno-rtti")

# # Pass flags to the Global environemnt (project `src` files, frameworks)
# global_env = DefaultEnvironment()
# global_env.Append(CPPDEFINES=[("TEST_GLOBAL", 1)])

# # Attach post action to the global environemnt

# def post_program_action(source, target, env):
#     print("Program has been built!")
#     program_path = target[0].get_abspath()
#     print("Program path", program_path)
#     # Use case: sign a firmware, do any manipulations with ELF, etc
#     # env.Execute(f"sign --elf {program_path}")

# global_env.AddPostAction("$PROGPATH", post_program_action)