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
# Move functions into their class namespaces, from the list the symbol matching writes
# (driving-symbol-matching/results/namespace-moves.json; regenerate it with
# `python apply.py --pending-namespaces` in that folder).
#
# Only namespace moves, only for the open program, and only where the function at the address still has the
# bare name the list expects; anything else is reported and left alone. Missing namespaces are created
# (nested ones too: "EAGLAnim::FnAnim"). It lists what it will do and asks before changing anything; the whole
# run is one undoable step (Edit > Undo).
# @category: Nightfire
# @runtime PyGhidra
import json
import os

from ghidra.program.model.symbol import SourceType


def split(qualified):
    # Top-level "::" only: in "URefCounter<ActTextureDatabase::TextureInfo>::URefCounter" the inner "::"
    # belongs to the template argument (same as driving-symbol-matching/lib/names.py).
    parts, depth, cur, i = [], 0, "", 0
    while i < len(qualified):
        c = qualified[i]
        if c == "<":
            depth += 1
        elif c == ">":
            depth -= 1
        if depth == 0 and qualified.startswith("::", i):
            parts.append(cur)
            cur, i = "", i + 2
            continue
        cur += c
        i += 1
    parts.append(cur)
    return parts


def namespace_for(parts):
    # Find or create each level in turn (NamespaceUtils.createNamespaceHierarchy splits on every "::").
    table = currentProgram.getSymbolTable()
    ns = currentProgram.getGlobalNamespace()
    for part in parts:
        found = table.getNamespace(part, ns)
        ns = found if found is not None else table.createNameSpace(ns, part, SourceType.USER_DEFINED)
    return ns

moves_path = os.path.join(os.path.dirname(__file__), "../driving-symbol-matching/results/namespace-moves.json")
with open(moves_path) as f:
    moves = [m for m in json.load(f)["moves"] if m["program"] == currentProgram.name]
print("Program: %s, %d moves listed for it" % (currentProgram.name, len(moves)))

todo = []
for m in moves:
    parts = split(m["name"])
    path, bare = "::".join(parts[:-1]), parts[-1]
    func = getFunctionAt(toAddr(m["address"]))
    if func is None:
        print("  skip %s: no function there" % m["address"])
        continue
    if func.getName() != bare:
        print("  skip %s: named %s, not %s" % (m["address"], func.getName(), bare))
        continue
    if func.getParentNamespace().getName(True) == path:
        continue
    todo.append((func, parts[:-1], m))

for func, path, m in todo:
    print("  %s %s -> %s::%s" % (m["address"], func.getName(True), "::".join(path), func.getName()))

if not todo:
    print("Nothing to move")
elif askYesNo("Nightfire namespaces", "Move %d functions into their namespaces? (Edit > Undo reverts it)" % len(todo)):
    moved = 0
    for func, path, m in todo:
        try:
            func.setParentNamespace(namespace_for(path))
            moved += 1
        except Exception as e:
            print("  FAILED %s -> %s: %s" % (m["address"], "::".join(path), e))
    print("Moved %d of %d; re-run `python apply.py --pending-namespaces` to confirm" % (moved, len(todo)))
else:
    print("Cancelled; nothing changed")
