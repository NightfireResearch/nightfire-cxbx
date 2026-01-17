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
# Sync function definitions from the original PS2 code to fix types/definitions
# @category: Nightfire
# @runtime PyGhidra
import os
print("Current file: " + __file__)
data_loc = os.path.join(os.path.dirname(__file__), "../orig_data")


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

assert(side == "action", "We only have full canonical symbols for Action engine")

fm = currentProgram.getFunctionManager()
funcs = fm.getFunctions(True)

import ReadElfParser

with open(f"{data_loc}/symbols.log", "r") as f:
    orig_functions, orig_objects, orig_others = ReadElfParser.parse_symtab(f.read())


# For each function in Ghidra, check it

num_unidentified = 0
num_unmatchable = 0

for ghidra_f in funcs:

    name = ghidra_f.getName()
    # Unidentified functions can't be compared
    if name.startswith("FUN_"):
        num_unidentified += 1
        continue

    matches = [x for x in orig_functions if x.type == "FUNC" and x.name == name]

    # Functions with a name that doesn't match exactly one entry in the original
    if len(matches) != 1:
        num_unmatchable += 1
        continue

    orig_f = matches[0]
    
    print(f"Expected signature for {name}: {orig_f.args}, Ghidra has {ghidra_f.parameters}")

    # For some reason, some of the original functions don't have a list of arguments given, 
    # and others are explicitly void. We need to deal with that appropriately.
    # eg read, write, close, isatty, _EnableIntc are all not given any args
    # whereas CreateSortedTextures(void), Mission_MonitorObjectives(void) etc
    # Presumably precompiled library code somehow lost this info?

    # TODO: If we aren't confident in the function signature, we can't do anything more here

    # If the length does not match, there's definitely something wrong
    if len(orig_f.args) != len(ghidra_f.parameters):
        print(f"Mismatch in argument lengths for {name}: Ghidra has {len(ghidra_f.parameters)} but original code has {len(orig_f.args)}")

    # If the parameters differ in type (other than const-ness), maybe something wrong
    # but we need to be aware of aliases (ULONG = unsigned long = ulong etc)


    isLTCG = ghidra_f.hasCustomVariableStorage()
    # ghidra_f.parameters: array
    # ghidra_f.callingConvention: "_cdecl" etc

    # Can apply 'setComment' to annotate completed funcs?



