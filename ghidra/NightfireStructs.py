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
# Apply struct layouts and function prototypes from the symbol matching's struct work
# (driving-symbol-matching/results/structs/*.json, every file whose "program" is the open program).
#
# Each file: {"program": "Driving.xbe",
#             "types": [{"name": "RSceneObj", "size": 64, "fields": [{"offset": 0, "type": "RSceneObj_vtbl *",
#                        "name": "vtable", "comment": "..."}], "comment": "..."}],
#             "prototypes": [{"address": "0x8dc60", "calling_convention": "__thiscall" or null (keep),
#                             "return": null (keep) or type, "params": [{"name": "m", "type": "MATRIX4 *"}]}]}
# Types: a name (as the Data Type Manager shows it, e.g. "CARP::Instance", "RAnimEngine::Handle"), then "*"s
# and/or "[n]". A struct that exists is rebuilt in place (its references stay); a missing one is created in the
# root category. Fields not listed stay undefined. It lists what it will do and asks first; one undoable step.
# @category: Nightfire
# @runtime PyGhidra
import glob
import json
import os
import re

from ghidra.program.model.data import (ArrayDataType, CategoryPath, DataTypeConflictHandler, PointerDataType,
                                       StructureDataType, Undefined1DataType, FunctionDefinitionDataType,
                                       ParameterDefinitionImpl)
from ghidra.program.model.data import BuiltInDataTypeManager
from ghidra.app.cmd.function import ApplyFunctionSignatureCmd
from ghidra.program.model.symbol import SourceType
from java.util import ArrayList

dtm = currentProgram.getDataTypeManager()
builtin = BuiltInDataTypeManager.getDataTypeManager()
here = os.path.dirname(__file__)
files = sorted(glob.glob(os.path.join(here, "../driving-symbol-matching/results/structs/*.json")))
docs = []
for p in files:
    with open(p) as f:
        d = json.load(f)
    if d.get("program") == currentProgram.getName():
        docs.append((os.path.basename(p), d))
print("Program %s: %d struct files" % (currentProgram.getName(), len(docs)))

ALIASES = {"byte": "byte", "ubyte": "byte", "bool": "bool", "char": "char", "short": "short", "ushort": "ushort",
           "int": "int", "uint": "uint", "long": "long", "ulong": "ulong", "longlong": "longlong",
           "ulonglong": "ulonglong", "float": "float", "double": "double", "void": "void",
           "unsigned int": "uint", "unsigned short": "ushort", "unsigned char": "uchar", "unsigned long": "ulong"}


def find_named(name):
    """A non-builtin type by display name, preferring the root category and the largest definition."""
    found = ArrayList()
    dtm.findDataTypes(name, found)
    cands = [t for t in found if t.getName() == name]
    if not cands and "::" in name:
        # Namespaced types can live as <leaf> in category /<Namespace>/... (e.g. /RAnimEngine/Handle)
        parts = name.split("::")
        dt = dtm.getDataType(CategoryPath("/" + "/".join(parts[:-1])), parts[-1])
        if dt is not None:
            return dt
    if not cands:
        return None
    cands.sort(key=lambda t: (str(t.getCategoryPath()) != "/", -max(t.getLength(), 0)))
    return cands[0]


def resolve(spec):
    s = spec.strip().replace("const ", "")
    arrays = [int(n) for n in re.findall(r"\[(\d+)\]", s)]
    s = re.sub(r"\[\d+\]", "", s).strip()
    stars = len(s) - len(s.rstrip("*"))
    base = s.rstrip("*").strip()
    if base in ALIASES:
        found = ArrayList()
        builtin.findDataTypes(ALIASES[base], found)
        dt = found[0] if found.size() else None
    elif re.match(r"undefined(\d)?$", base):
        found = ArrayList()
        builtin.findDataTypes(base, found)
        dt = found[0] if found.size() else Undefined1DataType.dataType
    else:
        dt = find_named(base)
    if dt is None:
        raise Exception("unknown type '%s'" % spec)
    for _ in range(stars):
        dt = PointerDataType(dt, 4, dtm)
    for n in reversed(arrays):
        dt = ArrayDataType(dt, n, dt.getLength(), dtm)
    return dt


plan_types, plan_protos = [], []
for fname, d in docs:
    for t in d.get("types", []):
        plan_types.append((fname, t))
    for p in d.get("prototypes", []):
        plan_protos.append((fname, p))
for fname, t in plan_types:
    have = find_named(t["name"])
    if t.get("if_missing") and have:
        continue
    print("  type %-32s %s, %d bytes, %d fields (%s)" % (t["name"], "rebuild" if have else "create", t["size"],
                                                        len(t["fields"]), fname))
print("  %d prototypes" % len(plan_protos))

if not (plan_types or plan_protos):
    print("Nothing to do")
elif askYesNo("Nightfire structs", "Apply %d types and %d prototypes? (Edit > Undo reverts it)" % (len(plan_types), len(plan_protos))):
    # Pass 1: make sure every named struct exists (so fields can point at each other), at its size.
    for fname, t in plan_types:
        if find_named(t["name"]) is None:
            # New namespaced types go in /<Namespace>/ as <leaf>, which decompiles as the plain leaf name.
            parts = t["name"].split("::")
            cat = CategoryPath("/" + "/".join(parts[:-1])) if len(parts) > 1 else CategoryPath("/")
            dtm.addDataType(StructureDataType(cat, parts[-1], t["size"], dtm), DataTypeConflictHandler.KEEP_HANDLER)
            t["_created"] = True
    errors = 0
    # Pass 2: rebuild each in place ("if_missing" placeholders only when this run created them).
    for fname, t in plan_types:
        if t.get("if_missing") and not t.get("_created"):
            continue
        st = find_named(t["name"])
        try:
            st.deleteAll()
            st.growStructure(t["size"])
            for f in sorted(t["fields"], key=lambda f: int(str(f["offset"]), 0)):
                dt = resolve(f["type"])
                st.replaceAtOffset(int(str(f["offset"]), 0), dt, dt.getLength(), f["name"], f.get("comment"))
            if st.getLength() != t["size"]:
                print("  WARNING %s is %d bytes, expected %d" % (t["name"], st.getLength(), t["size"]))
            if t.get("comment"):
                st.setDescription(t["comment"])
        except Exception as ex:
            errors += 1
            print("  FAILED type %s: %s" % (t["name"], ex))
    # Pass 3: prototypes. Return type and calling convention are kept unless given; 'this' comes from the
    # function's class namespace for __thiscall.
    done = 0
    for fname, p in plan_protos:
        try:
            addr = toAddr(p["address"])
            fn = getFunctionAt(addr)
            if fn is None:
                raise Exception("no function")
            sig = FunctionDefinitionDataType(fn.getName(), dtm)
            sig.setReturnType(resolve(p["return"]) if p.get("return") else fn.getReturnType())
            params = [ParameterDefinitionImpl(q["name"], resolve(q["type"]), None) for q in p["params"]]
            sig.setArguments(params)
            if p.get("calling_convention"):
                sig.setCallingConvention(p["calling_convention"])
            else:
                sig.setCallingConvention(fn.getCallingConventionName())
            cmd = ApplyFunctionSignatureCmd(addr, sig, SourceType.USER_DEFINED)
            if not cmd.applyTo(currentProgram):
                raise Exception(cmd.getStatusMsg())
            done += 1
        except Exception as ex:
            errors += 1
            print("  FAILED prototype %s: %s" % (p["address"], ex))
    print("Applied %d types, %d prototypes, %d errors" % (len(plan_types), done, errors))
else:
    print("Cancelled; nothing changed")
