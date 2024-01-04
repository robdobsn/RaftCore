import argparse
import json
from pathlib import Path
from collections.abc import Mapping, Sequence, Set
def objwalk(obj, path=(), memo=None):
    if memo is None:
        memo = set()
    if isinstance(obj, Mapping):
        if id(obj) not in memo:
            memo.add(id(obj)) 
            for key, value in obj.items():
                for child in objwalk(value, path + (key,), memo):
                    yield child
    elif isinstance(obj, (Sequence, Set)) and not isinstance(obj, str):
        if id(obj) not in memo:
            memo.add(id(obj))
            for index, value in enumerate(obj):
                for child in objwalk(value, path + (index,), memo):
                    yield child
    else:
        yield path, obj

def main():
    # Read the SysTypes.json file
    sysTypesJsonPath = args.sysTypesJsonFolder / "SysTypes.json"
    sysTypesJson = sysTypesJsonPath.read_text()

    # Parse the Json and check valid
    sysTypes = json.loads(sysTypesJson)
    if not sysTypes:
        raise ValueError(f"Invalid SysTypes.json: '{sysTypesJsonPath}'")
    
    # Versioned values are stored in an array of objects where each object has
    # a "__hwRevs__" key and a "__value__" key. The "__hwRevs__" key is an
    # array of numbers representing the hardware revisions that the value
    # applies to. The "__value__" key is the value itself which may be any 
    # valid Json value.

    # Generate a single base file which contains the "__value__" key
    # corresponding to the __hwRevs__ key with the highest value

    # Generate a separate JSON file for each hardware revision which contains 
    # the "__value__" key corresponding to the __hwRevs__ key with each value
    # that is found

    # Create a list of sets of paths which are versioned
    versionedPaths = []
    leafNodeHwRevs = {}
    # Extract all used __hwRevs__ numbers
    hwRevs = set()
    # Recurse over the sysTypes object
    for path, value in objwalk(sysTypes):
        # print(path, value)
        if "__hwRevs__" in path or "__value__" in path:
            # print(f"Found hwRevs value '{value}' at path '{path}'")
            # Add each part of path to set of versioned paths at that level
            pathStr = ""
            for i in range(len(path)):
                if i > 0:
                    pathStr += "/"
                pathStr += str(path[i])
                if i >= len(versionedPaths):
                    versionedPaths.append(set())
                versionedPaths[i].add(pathStr)
            # Add to set of hwRevs numbers
            if "__hwRevs__" in path:
                hwRevs.add(int(value))
                # Add to leafNodeHwRevs
                # if path[-1] == "__hwRevs__":
                if value not in leafNodeHwRevs:
                    leafNodeHwRevs[value] = set()
                leafNodeHwRevs[value].add(path[:-2])
                # print(f"Found hwRevs value '{value}' at path '{path}'")

    # Print set of hwRevs
    print(hwRevs)










#     hwRevs = set()
#     hwRevs.add(1)













#     # Print list of sets of versioned paths
#     print(versionedPaths)

#     # Generate an object containing a key for each hwRev and a copy of the
#     # sysTypes object as the value
#     hwRevObjs = {x : sysTypes.copy() for x in hwRevs}
#     print(hwRevObjs)



#     # Recurse over each of the hwRevObjs and remove all values which do not
#     # apply to that hwRev
#     for rev in hwRevObjs:
#         paths_to_remove = []
#         path_strs_to_remove = []
#         for path, value in objwalk(hwRevObjs[rev]):
#             if len(path) < 1:
#                 continue
#             pathStr = ""
#             path_already_handled = False
#             for i in range(len(path)):
#                 if i > 0:
#                     pathStr += "/"
#                 pathStr += str(path[i])
#                 if pathStr in paths_to_remove:
#                     path_already_handled = True
#                     break
#                 # Remove all objects that are not versioned at any level
#                 if pathStr not in versionedPaths[i] and pathStr not in path_strs_to_remove:
#                     print(f"Removing path '{pathStr}'")
#                     paths_to_remove.append(path[:i+1])
#                     path_strs_to_remove.append(pathStr)
#                     path_already_handled = True
#                     break
#             if path_already_handled:
#                 continue
#             # # print(path, value)
#             # elif path[-2] == "__hwRevs__":
#             #     # Extract the object at this path
#             #     obj = hwRevObjs[rev]
#             #     for key in path[:-1]:
#             #         obj = obj[key]
#             #     print(f"Found hwRevs value '{value}' at path '{path}' with parent object '{obj}'")
#             #     # Populate the hwRevObjs for each value in the __hwRevs__ array with the contents
#             #     # of the __value__ key
#             #     # for hwRev in obj["__hwRevs__"]:
#             #     #     hwRevObjs[hwRev][path[-3]] = obj["__value__"]

#         # Remove all objects which do not apply to this hwRev
#         for path in paths_to_remove[::-1]:
#             print(f"Removing object '{path}'")
#             obj = hwRevObjs[rev]
#             for key in path[:-1]:
#                 obj = obj[key]
#             del obj[path[-1]]

#         # # Go through the versioned paths which have __hwRevs__ keys
#         # for versionedPathAtLevel in versionedPaths:
#         #     # Check if the last element of the path is __hwRevs__
#         #     for versionedPath in versionedPathAtLevel:
#         #         if versionedPath.split("/")[-1] == "__hwRevs__":
#         #             print(f"versionedPath: {versionedPath}")

#         #             # Extract the object at this path
#         #             obj = hwRevObjs[rev]
#         #             for key in versionedPath.split("/")[:-1]:
#         #                 if type(obj) is list:
#         #                     obj = obj[int(key)]
#         #                 else:
#         #                     obj = obj[key]

#         #             # Check if the __hwRevs__ key is an array
#         #             if not isinstance(obj["__hwRevs__"], list):
#         #                 raise ValueError(f"__hwRevs__ key at path '{versionedPath}' is not an array")
                    
            
#         # # Now walk the hwRevObjs and turn all the __hwRevs__ keys into __value__ keys
#         # any_changes_made = True
#         # while any_changes_made:
#         #     any_changes_made = False
#         #     for path, value in objwalk(hwRevObjs[rev]):
#         #         if len(path) < 2:
#         #             continue
#         #         # print(path, value)
#         #         elif path[-2] == "__hwRevs__":
#         #             # Extract the object at this path
#         #             obj = hwRevObjs[rev]
#         #             for key in path[:-5]:
#         #                 obj = obj[key]
#         #             # print(f"Found hwRevs '{value}' at path '{path}' with lastKey {path[-4]} value {obj[path[-5]][path[-4]][path[-3]]['__value__']} idx {path[-3]} with parent object '{obj[path[-5]]}'")
#         #             # if value == rev:
#         #             #     tmpVal = obj[path[-5]][path[-4]][path[-3]]['__value__']
#         #             #     print(f"Setting value '{obj[path[-5]][path[-4]][path[-3]]['__value__']}' at path '{path[:-4]}' current value '{obj[path[-5]][path[-4]]}'")
#         #             #     print(type(obj[path[-5]][path[-4]]))
#         #             #     a = obj[path[-5]]
#         #             #     a[path[-4]] = tmpVal
#         #             #     any_changes_made = True
#         #             #     break
#         #                 # obj[path[-4]] = "BANANAS"
#         #                 # obj[path[-4]][path[-3]]['__value__']
#         #                 # obj[path[-4]] = obj[path[-1]]
                        
#         #             # Populate the hwRevObjs for each value in the __hwRevs__ array with the contents
#         #             # of the __value__ key
#         #             # for hwRev in obj["__hwRevs__"]:
#         #             #     hwRevObjs[hwRev][path[-3]] = obj["__value__"]

#     # # # Debug
#     # # # print(f"baseObj: {baseObj}")
#     print(f"hwRevObjs[1]: {json.dumps(hwRevObjs[1])}")










if __name__ == "__main__":
    # Add the folder containing the SysTypes.json file to the argument list
    parser = argparse.ArgumentParser()
    parser.add_argument('sysTypesJsonFolder',
                        type=Path,
                        help="Path to folder containing SysTypes.json")
    args = parser.parse_args()

    main()
