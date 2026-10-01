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
# Export every match in the PS2_To_Xbox Version Tracking session (accepted or not) for the symbol matching
# to check: driving-symbol-matching/data/vt-matches.json. Read-only: the session is opened, read and
# released without saving. Run from any CodeBrowser; the session may also be open in the VT tool.
# @category: Nightfire
# @runtime PyGhidra
import json
import os

SESSION = "/PS2_To_Xbox"
out_path = os.path.join(os.path.dirname(__file__), "../driving-symbol-matching/data/vt-matches.json")

df = state.getProject().getProjectData().getFile(SESSION)
if df is None:
    raise Exception("No project file " + SESSION)
# Open in the VT tool: borrow that instance (it includes unsaved work); a second getDomainObject would fail
# with "Domain object(s) are busy/locked" while the VT tool holds it.
# A script runs inside a transaction on currentProgram. When that program is the session's source or
# destination, opening the session can't lock it ("busy/locked"), so close the script's own (empty)
# transaction first. This script changes nothing in currentProgram.
try:
    end(True)
except Exception as ex:
    print("Could not end the script transaction (%s); if opening fails, run this from a CodeBrowser "
          "showing some other program, e.g. /Similar_Games/AUF/driving.elf" % ex)
session = df.getOpenedDomainObject(this)
how = "borrowed from the open VT tool"
if session is None:
    # VT sessions don't support read-only opening; open normally, never save, release at the end.
    session = df.getDomainObject(this, False, False, monitor)
    how = "opened here (not saved)"
print("Session %s: %s" % (SESSION, how))
try:
    src, dst = session.getSourceProgram(), session.getDestinationProgram()

    def fname(program, addr, is_function):
        if is_function:
            f = program.getFunctionManager().getFunctionAt(addr)
            if f is not None:
                ns = f.getParentNamespace()
                return f.getName() if ns.isGlobal() else ns.getName(True) + "::" + f.getName()
        s = program.getSymbolTable().getPrimarySymbol(addr)
        return s.getName(True) if s is not None else None

    matches = []
    for ms in session.getMatchSets():
        corr = ms.getProgramCorrelatorInfo().getName()
        for m in ms.getMatches():
            a = m.getAssociation()
            is_fn = str(a.getType()) == "FUNCTION"
            sim, conf = m.getSimilarityScore(), m.getConfidenceScore()
            matches.append({
                "correlator": corr,
                "type": str(a.getType()),
                "status": str(a.getStatus()),
                "ps2": "0x%08x" % a.getSourceAddress().getOffset(),
                "xbox": "0x%08x" % a.getDestinationAddress().getOffset(),
                "ps2_name": fname(src, a.getSourceAddress(), is_fn),
                "xbox_name": fname(dst, a.getDestinationAddress(), is_fn),
                "similarity": sim.getScore() if sim is not None else None,
                "confidence": conf.getScore() if conf is not None else None,
                "ps2_length": m.getSourceLength(),
                "xbox_length": m.getDestinationLength(),
            })
    with open(out_path, "w") as f:
        json.dump({"session": SESSION, "source": src.getName(), "destination": dst.getName(),
                   "matches": matches}, f, indent=0)
    print("Wrote %d matches from %d match sets to %s" % (len(matches), len(list(session.getMatchSets())), out_path))
finally:
    session.release(this)
