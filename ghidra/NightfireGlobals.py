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
# Label global data (class statics, file globals) from the symbol matching's globals work
# (driving-symbol-matching/results/globals/*-<program>.json, every file whose "program" is the open program).
#
# Each file: {"program": "Driving.xbe", "labels": [{"address": "0x1ebff4", "name": "RRenderer::fgRenderer",
#                                                   "comment": "..."}]}
# A name "A::B::leaf" becomes label "leaf" in namespace A::B (created if missing). The new label is made
# primary. An address whose primary label is already a non-default name different from the new one is left
# alone and reported. The comment goes in an EOL comment tagged [globals]. It lists what it will do and asks
# first; one undoable step.
# @category: Nightfire
# @runtime PyGhidra
import glob
import json
import os

from ghidra.app.util import NamespaceUtils
from ghidra.program.model.listing import CodeUnit
from ghidra.program.model.symbol import SourceType

here = os.path.dirname(__file__)
st = currentProgram.getSymbolTable()
listing = currentProgram.getListing()
key = {"Driving.xbe": "driving", "DRIVING.ELF": "driving"}.get(currentProgram.getName(), "")
labels = []
for p in sorted(glob.glob(os.path.join(here, "../driving-symbol-matching/results/globals/*.json"))):
    with open(p) as f:
        d = json.load(f)
    if isinstance(d, dict) and d.get("program") == currentProgram.getName():
        labels += [(os.path.basename(p), l) for l in d["labels"]]
print("Program %s: %d labels" % (currentProgram.getName(), len(labels)))


def is_default(sym):
    return sym is None or sym.getSource() == SourceType.DEFAULT or sym.getName().startswith(("DAT_", "PTR_", "s_", "u_", "BYTE_", "WORD_", "DWORD_", "FLOAT_"))


plan, skip = [], []
for fname, l in labels:
    addr = toAddr(l["address"])
    cur = st.getPrimarySymbol(addr)
    if cur is not None and cur.getName(True) == l["name"]:
        continue
    if not is_default(cur):
        skip.append((l, cur.getName(True)))
        continue
    plan.append((addr, l))
for addr, l in plan:
    print("  %s %s" % (addr, l["name"]))
for l, have in skip:
    print("  SKIP %s %s: already named %s" % (l["address"], l["name"], have))

if not plan:
    print("Nothing to do")
elif askYesNo("Nightfire globals", "Create %d labels (%d skipped)? (Edit > Undo reverts it)" % (len(plan), len(skip))):
    done = errors = 0
    for addr, l in plan:
        try:
            parts = l["name"].split("::")
            ns = currentProgram.getGlobalNamespace()
            if len(parts) > 1:
                ns = NamespaceUtils.createNamespaceHierarchy("::".join(parts[:-1]), None, currentProgram,
                                                             SourceType.USER_DEFINED)
            sym = st.createLabel(addr, parts[-1], ns, SourceType.USER_DEFINED)
            sym.setPrimary()
            if l.get("comment"):
                old = listing.getComment(CodeUnit.EOL_COMMENT, addr) or ""
                keep = old.split("[globals]")[0].rstrip()
                listing.setComment(addr, CodeUnit.EOL_COMMENT, (keep + "\n" if keep else "") + "[globals] " + l["comment"])
            done += 1
        except Exception as ex:
            errors += 1
            print("  FAILED %s %s: %s" % (addr, l["name"], ex))
    print("Created %d labels, %d errors, %d skipped" % (done, errors, len(skip)))
else:
    print("Cancelled; nothing changed")
