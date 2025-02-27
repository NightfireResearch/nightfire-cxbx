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
import pathlib
print("Current file: " + __file__)
json_loc = os.path.join(os.path.dirname(__file__), "../tools/functions.json")
print("Location of JSON: " + json_loc)

# Our goal is to produce a list of all functions, sorted by address, as a JSON file, with each entry having:
# {"return_type": "undefined", "address": "0x00011000", "calling_convention": "unknown", "param_types": [], "prototype_string": "undefined FUN_00011000()", "name": "FUN_00011000"}


print("Name: " + currentProgram.name)

if currentProgram.name == "default.xbe": # Xbox Action engine
    print("Xbox Action engine")
elif currentProgram.name == "Driving.xbe": # Xbox Driving engine
    print("Xbox Driving engine - WIP")
    exit(1)
else:
    print("Unknown program - designed for Xbox Action and Driving only")
    exit(1)

fm = currentProgram.getFunctionManager()
funcs = fm.getFunctions(True)

function_list = []
for f in funcs:
    name = f.getName()
    address = f.getEntryPoint()
    calling_convention = f.getCallingConventionName()
    return_type = f.getReturnType().getName()
    param_types = [str(x.getDataType()) for x in f.getParameters()]
    prototype_string = f.getPrototypeString(True, True)
    function_dict = {
        "name": name,
        "address": "0x" + str(address),
        "calling_convention": calling_convention,
        "return_type": return_type,
        "param_types": param_types,
        "prototype_string": prototype_string,
    }
    function_list.append(function_dict)

function_list.sort(key=lambda x: x["address"])

import json
with open(json_loc, "w") as outfile:
    # We want to make each entry human-readable so that diffs look nice
    json.dump(function_list, outfile, indent=4)
