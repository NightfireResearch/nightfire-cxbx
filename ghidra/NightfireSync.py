# Credits: Nightfire Research Team - (2024 - 2025)

## ###
# IP: GHIDRA
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##
# Sync functions between Ghidra and Nightfire-CXBX
# @category: Nightfire
# @runtime PyGhidra
import os

def paramToStr(x):
    raw = str(x.getDataType())
    result = raw
    chunks = raw.split(" ")
    if len(chunks) > 0 and chunks[0] == "typedef":
        result = chunks[1]
    return result    


def export_json(side):
    function_list = []
    for f in funcs:
        param_types = [paramToStr(x) for x in f.getParameters()]
        # Full namespace path ("EAGLAnim::FnAnim::Eval"), not just the innermost namespace
        ns = f.getParentNamespace()
        ns_path = "Global" if ns.isGlobal() else ns.getName(True)
        function_dict = {
            "name": f.getName() if ns.isGlobal() else ns_path + "::" + f.getName(),
            "address": "0x" + str(f.getEntryPoint()),
            "calling_convention": f.getCallingConventionName(),
            "has_custom_variable_storage": f.hasCustomVariableStorage(),
            "return_type": f.getReturnType().getName(),
            "param_types": param_types,
            "prototype_string": f.getPrototypeString(True, True),
            "parameters": [str(x) for x in f.getParameters()],
            "is_thunk": f.isThunk(),
            "namespace": ns_path
        }
        function_list.append(function_dict)

    function_list.sort(key=lambda x: x["address"])

    import json

    with open(f"{json_loc}_{side}.json", "w") as outfile:
        # We want to make each entry human-readable so that diffs look nice
        json.dump(function_list, outfile, indent=4)

def export_structs(side):
    # Every structure in the program's own data types, for the overlay classes that stand in for them
    # (src/common/xbeClass.h): tools/preprocess.py generates each class's fields, at these offsets, from this.
    # Types are Ghidra's display names ("Schedule *", "undefined4", "TaskRecord_LListEntry *[8]"), which the
    # generator translates; bit-fields are listed with "bitfield": true and become plain bytes.
    structs = []
    for dt in currentProgram.getDataTypeManager().getAllStructures():
        if dt.isNotYetDefined() or dt.getLength() <= 0:
            continue
        fields = []
        for c in dt.getDefinedComponents():
            name = c.getFieldName()
            if name is None or name == "":
                name = c.getDefaultFieldName()
            fields.append({
                "offset": c.getOffset(),
                "size": c.getLength(),
                "name": name,
                "type": c.getDataType().getDisplayName(),
                "bitfield": c.isBitFieldComponent(),
            })
        structs.append({
            "name": dt.getName(),
            "category": str(dt.getCategoryPath()),
            "size": dt.getLength(),
            "fields": fields,
        })

    structs.sort(key=lambda x: (x["name"], x["category"]))

    import json

    with open(f"{structs_loc}_{side}.json", "w") as outfile:
        json.dump(structs, outfile, indent=1)
    print(f"Exported {len(structs)} structures")

print("Current file: " + __file__)
structs_loc = os.path.join(os.path.dirname(__file__), "../tools/structs")
json_loc = os.path.join(os.path.dirname(__file__), "../tools/functions")
print("Location of JSON: " + json_loc)

# Our goal is to produce a list of all functions, sorted by address, as a JSON file, with each entry having:
# {"return_type": "undefined", "address": "0x00011000", "calling_convention": "unknown", "param_types": [], "prototype_string": "undefined FUN_00011000()", "name": "FUN_00011000"}


print("Name: " + currentProgram.name)
side = "none"
if currentProgram.name == "default.xbe": # Xbox Action engine
    print("Xbox Action engine")
    side = "action"
elif currentProgram.name == "Driving.xbe": # Xbox Driving engine
    print("Xbox Driving engine")
    side = "driving"
else:
    print("Unknown program - designed for Xbox Action and Driving only")
    exit(1)

fm = currentProgram.getFunctionManager()
funcs = fm.getFunctions(True)

export_json(side)

export_structs(side)
