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
# Convert namespaces that are C++ classes into Ghidra classes, from the list the symbol matching writes
# (driving-symbol-matching/results/class-namespaces.json; regenerate it with `python classes.py` in that
# folder). The list comes from the symbol file: a namespace is a class when the file has its vtable, type_info,
# a constructor or destructor, or a const method.
#
# Only for the open program; a path that doesn't resolve to exactly one plain namespace is reported and left.
# It lists what it will convert and asks first; the whole run is one undoable step (Edit > Undo).
# Every run, even a cancelled one, writes a census of the program's namespaces and classes (full paths) and what
# happened to each listed entry to driving-symbol-matching/results/namespace-census-<program>.json: the MCP
# server can't tell a nested class from a namespace, so classes.py reads this instead.
# @category: Nightfire
# @runtime PyGhidra
import json
import os

from ghidra.app.util import NamespaceUtils
from ghidra.program.model.symbol import SymbolType

list_path = os.path.join(os.path.dirname(__file__), "../driving-symbol-matching/results/class-namespaces.json")
with open(list_path) as f:
    entries = json.load(f).get(currentProgram.name, [])
print("Program: %s, %d namespaces listed for it" % (currentProgram.name, len(entries)))

outcome = {}
todo = []
for e in entries:
    found = NamespaceUtils.getNamespaceByPath(currentProgram, None, e["path"])
    found = [ns for ns in found] if found is not None else []
    if len(found) != 1:
        print("  skip %s: %d namespaces with that path" % (e["path"], len(found)))
        outcome[e["path"]] = "skipped: %d namespaces with that path" % len(found)
        continue
    ns = found[0]
    kind = ns.getSymbol().getSymbolType()
    if kind == SymbolType.CLASS:
        outcome[e["path"]] = "already a class"
        continue
    if kind != SymbolType.NAMESPACE:
        print("  skip %s: it is a %s" % (e["path"], kind))
        outcome[e["path"]] = "skipped: it is a %s" % kind
        continue
    todo.append((ns, e))

for ns, e in todo:
    print("  %s  (%s)" % (e["path"], ", ".join(e["evidence"])))

if not todo:
    print("Nothing to convert")
elif askYesNo("Nightfire classes", "Convert %d namespaces into classes? (Edit > Undo reverts it)" % len(todo)):
    done = 0
    for ns, e in todo:
        try:
            NamespaceUtils.convertNamespaceToClass(ns)
            done += 1
            outcome[e["path"]] = "converted"
        except Exception as ex:
            print("  FAILED %s: %s" % (e["path"], ex))
            outcome[e["path"]] = "FAILED: %s" % ex
    print("Converted %d of %d" % (done, len(todo)))
else:
    print("Cancelled; nothing changed")
    for ns, e in todo:
        outcome[e["path"]] = "cancelled"

# Walk the namespace tree from the global namespace (getAllSymbols returned none of them).
census = {"namespace": [], "class": []}
table = currentProgram.getSymbolTable()
stack = [currentProgram.getGlobalNamespace().getSymbol()]
while stack:
    for child in table.getChildren(stack.pop()):
        kind = child.getSymbolType()
        if kind == SymbolType.CLASS or kind == SymbolType.NAMESPACE:
            census["class" if kind == SymbolType.CLASS else "namespace"].append(child.getObject().getName(True))
            stack.append(child)
class_count = sum(1 for _ in table.getClassNamespaces())
census_path = os.path.join(os.path.dirname(list_path), "namespace-census-%s.json" % currentProgram.name)
with open(census_path, "w") as f:
    json.dump({"program": currentProgram.name, "class_namespaces_count": class_count,
               "classes": sorted(census["class"]),
               "namespaces": sorted(census["namespace"]), "outcome": outcome}, f, indent=1)
print("Census: %d classes (Ghidra counts %d), %d plain namespaces -> %s"
      % (len(census["class"]), class_count, len(census["namespace"]), census_path))
