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
# Create functions that Ghidra folded into the body of the function before them (often because the new
# function's first word starts 00 00 and was taken for padding), from the list the symbol matching writes
# (driving-symbol-matching/results/function-splits.json). Names are applied afterwards by the matching's
# own batch; this only makes the functions exist.
#
# For each address, in ascending order: if a function already starts there, skip it; if one contains it,
# shrink that function's body to end just before the address; then create a function at the address.
# Only for the open program. It lists what it will do and asks first; the whole run is one undoable step.
# @category: Nightfire
# @runtime PyGhidra
import json
import os

from ghidra.program.model.address import AddressSet

list_path = os.path.join(os.path.dirname(__file__), "../driving-symbol-matching/results/function-splits.json")
with open(list_path) as f:
    data = json.load(f)
entries = [e for e in data["splits"] if data.get("program", "DRIVING.ELF") == currentProgram.name]
entries.sort(key=lambda e: int(e["address"], 16))
print("Program: %s, %d addresses listed" % (currentProgram.name, len(entries)))

todo = []
for e in entries:
    a = toAddr(e["address"])
    if getFunctionAt(a) is not None:
        continue
    outer = getFunctionContaining(a)
    todo.append((a, outer, e))
    print("  %s %s%s" % (e["address"], e.get("name", ""),
                         (" (split from %s)" % outer.getName(True)) if outer is not None else " (free)"))

if not todo:
    print("Nothing to do")
elif askYesNo("Nightfire splits", "Create %d functions, shrinking the functions that contain them? (Edit > Undo reverts it)" % len(todo)):
    made = 0
    for a, outer, e in todo:
        try:
            outer = getFunctionContaining(a)   # re-read: an earlier split may have changed it
            if outer is not None and outer.getEntryPoint() != a:
                keep = outer.getBody().intersect(AddressSet(outer.getEntryPoint(), a.subtract(1)))
                outer.setBody(keep)
            fn = createFunction(a, None)
            if fn is None:
                print("  FAILED %s: createFunction returned nothing" % e["address"])
            else:
                made += 1
        except Exception as ex:
            print("  FAILED %s: %s" % (e["address"], ex))
    print("Created %d of %d" % (made, len(todo)))
else:
    print("Cancelled; nothing changed")
