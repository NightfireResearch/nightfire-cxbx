"""Apply an approved batch of names to Driving.xbe. Dry run unless --apply.

    python apply.py results/batches/batch-001.json            # dry run: checks and planned writes
    python apply.py results/batches/batch-001.json --apply    # snapshot, write, read back, log
    python apply.py --undo results/batches/batch-001.log.json [--apply]
    python apply.py results/batches/batch-001.json --namespaces  # checklist of by-hand namespace moves
    python apply.py --pending-namespaces    # results/namespace-moves.json, for ghidra/NightfireNamespaces.py

Checks before any write, all against live Ghidra:
  - the function's current name is still the one the batch was reviewed against ("expect"), so nothing
    renamed by hand since is overwritten; a function to create must not exist yet;
  - an old name that isn't FUN_ must not appear as Class::Method in src/driving (AUTOINJECT resolves by name);
  - the new name must not already be taken, in the program or the batch, unless the item allows it
    ("allow_duplicate": the symbol file itself gives the name to several functions - overloads).
One failed check stops the whole batch before any write.

Writes: create_function (if asked), rename_function_by_address, set_plate_comment. The plate comment gets a
managed block between "[symbol-matching]" and "[/symbol-matching]"; text outside it is kept, and a re-run
replaces the block. Each write is read back. The log records old and new values for --undo.
"""

import json
import os
import re
import subprocess
import sys
import time

from lib import ghidra_ro as g, names

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BEGIN, END = "[symbol-matching]", "[/symbol-matching]"


def live(address, program=g.XBOX):
    """(bare name, plate comment) at an address, or (None, None) if there is no function."""
    info = g.function_info(program, address)
    fn = info.get("function")
    if not fn or " at " not in fn:
        return None, None
    plate = g.get_json("get_plate_comment", program=program, address=f"0x{address:x}").get("comment")
    return fn.split(" at ")[0], plate


def block(item):
    lines = [BEGIN, f"Canonical name: {item['name']}"]
    if item.get("sheet_name") and item["sheet_name"] != item["name"]:
        lines.append(f"Symbol file name: {item['sheet_name']}")
    if item.get("also"):
        lines.append("Identical code folded by the linker; also: " + ", ".join(item["also"]))
    cls = names.namespace(item["name"]) or None
    if cls:
        lines.append(f"Class: {cls}")
    for e in item.get("evidence", []):
        lines.append("Evidence: " + e)
    lines.append(f"Batch: {item['batch']}")
    lines.append(END)
    return "\n".join(lines)


def merged_plate(old, new_block):
    old = old or ""
    if BEGIN in old and END in old:
        head, rest = old.split(BEGIN, 1)
        tail = rest.split(END, 1)[1]
        return (head + new_block + tail).strip()
    return (old.rstrip() + "\n\n" + new_block).strip() if old.strip() else new_block


def used_in_src(qualified):
    """Lines in src/driving naming the function: Class::Method, or a free function as a whole word."""
    flags = ["-F"] if "::" in qualified else ["-w", "-F"]
    r = subprocess.run(["git", "grep", "-n", *flags, qualified, "--", "src/driving"], cwd=ROOT,
                       capture_output=True, text=True)
    return r.stdout.splitlines()


def check(items, program):
    problems = []
    seen = {}
    # Every function's qualified name now, to refuse a new name that already exists at another address.
    existing = {}
    qualified = g.qualified_names(program)
    renamed = {int(it["xbox"], 16) for it in items if it.get("rename", True)}   # names this batch frees
    for a, n in g.functions(program):
        if a not in renamed:
            existing.setdefault(qualified.get(a, n), []).append(a)
    for it in items:
        a = int(it["xbox"], 16)
        have, _ = live(a, program)
        if it.get("create"):
            if have is not None:
                problems.append(f"{it['xbox']}: expected no function, found {have}")
        elif have != names.bare(it["expect"]):
            problems.append(f"{it['xbox']}: expected {it['expect']}, Ghidra has {have}")
        if program == g.XBOX and not it["expect"].startswith("FUN_") and it["expect"] != "(none)":
            refs = used_in_src(it["expect"])
            if refs:
                problems.append(f"{it['xbox']}: old name {it['expect']} is used in src/driving: {refs[:2]}")
        if it.get("rename", True) and re.search(r"[\s\x00-\x1f]", it["name"]):
            problems.append(f"{it['xbox']}: {it['name']!r} has whitespace or control characters, which Ghidra refuses")
        if it.get("rename", True) and not it.get("allow_duplicate"):
            elsewhere = [x for x in existing.get(it["name"], []) if x != int(it["xbox"], 16)]
            if elsewhere:
                problems.append(f"{it['xbox']}: {it['name']} already exists at {', '.join(hex(x) for x in elsewhere)}")
        if it["name"] in seen and not it.get("allow_duplicate"):
            problems.append(f"{it['xbox']}: {it['name']} also proposed for {seen[it['name']]}")
        seen[it["name"]] = it["xbox"]
    return problems


def run(batch_path, do_apply):
    from lib import ghidra_rw as w

    with open(batch_path) as f:
        batch = json.load(f)
    items = batch["items"]
    program = batch.get("program", g.XBOX)
    for it in items:
        it["batch"] = batch["batch"]
    problems = check(items, program)
    print(f"batch {batch['batch']} ({program}): {len(items)} items, {len(problems)} problems")
    for p in problems:
        print("  PROBLEM", p)
    for it in items:
        target = it["name"] if it.get("rename", True) else f"(keep name; plate only: {it['name']})"
        print(f"  {it['xbox']} {('create; ' if it.get('create') else '')}{it['expect']} -> {target}"
              + (f"  (+{len(it['also'])} folded)" if it.get("also") else ""))
    if problems or not do_apply:
        print("dry run" if not problems else "stopped: nothing written")
        return
    log_path = batch_path.replace(".json", ".log.json")
    if os.path.exists(log_path):
        with open(log_path) as f:
            if all(e.get("ok") for e in json.load(f)["items"]):
                print(f"already applied (see {log_path}); nothing written")
                return

    subprocess.run([sys.executable, os.path.join(HERE, "snapshot.py"), program], check=True, cwd=HERE)
    log = {"batch": batch["batch"], "program": program, "applied": time.ctime(), "items": []}
    log_path = batch_path.replace(".json", ".log.json")
    try:
        for it in items:
            a = int(it["xbox"], 16)
            old_name, old_plate = live(a, program)
            entry = {"xbox": it["xbox"], "old_name": old_name, "old_plate": old_plate, "created": False, "steps": []}
            log["items"].append(entry)
            if it.get("create"):
                entry["steps"].append(("create_function", w.post("create_function", {"address": it["xbox"]}, program)))
                entry["created"] = True
            bare = names.bare(it["name"]) if it.get("rename", True) else old_name
            if bare != old_name:
                result = w.post("rename_function_by_address",
                                {"function_address": it["xbox"], "new_name": bare, "strict_mode": "off"}, program)
                entry["steps"].append(("rename", result))
                if isinstance(result, dict) and result.get("error"):
                    entry["new_name"], entry["ok"] = old_name, False
                    print(f"  FAIL {it['xbox']} rename refused: {result['error']}")
                    print("  stopping at the first failure (plate comment left alone)")
                    break
            plate = merged_plate(old_plate, block(it))
            entry["steps"].append(("plate", w.post("set_plate_comment", {"address": it["xbox"], "comment": plate}, program)))
            got_name, got_plate = live(a, program)
            entry["new_name"], entry["ok"] = got_name, got_name == bare and BEGIN in (got_plate or "")
            print(f"  {'ok  ' if entry['ok'] else 'FAIL'} {it['xbox']} {old_name} -> {got_name}")
            if not entry["ok"]:
                print("  stopping at the first failure")
                break
    finally:
        with open(log_path, "w") as f:
            json.dump(log, f, indent=1)
        print(f"log: {log_path}")


def undo(log_path, do_apply):
    from lib import ghidra_rw as w

    with open(log_path) as f:
        log = json.load(f)
    program = log.get("program", g.XBOX)
    for e in reversed(log["items"]):
        print(f"  {e['xbox']} {e.get('new_name')} -> {e['old_name'] or '(created: rename to FUN_ default)'}")
        if not do_apply:
            continue
        name = e["old_name"] or f"FUN_{int(e['xbox'], 16):08x}"
        w.post("rename_function_by_address", {"function_address": e["xbox"], "new_name": name, "strict_mode": "off"}, program)
        w.post("set_plate_comment", {"address": e["xbox"], "comment": e["old_plate"] or ""}, program)
    print("dry run" if not do_apply else "undone (functions created by the batch are left in place)")


def namespaces(batch_path):
    """Which batch functions still need moving into their class (done by hand in Ghidra's Edit Function
    dialog, typing the full "Class::Name"). Reads live Ghidra; re-run to check the moves."""
    with open(batch_path) as f:
        batch = json.load(f)
    program = batch.get("program", g.XBOX)
    live_ns = g.qualified_names(program)
    lines = [f"# Batch {batch['batch']}: namespace moves", "",
             "In Ghidra: Edit Function (F) on each address, and set the name to the full text in the last column.", "",
             "| done | address | now | set name to |", "|---|---|---|---|"]
    pending = 0
    for it in batch["items"]:
        if "::" not in it["name"] or not it.get("rename", True):
            continue
        a = int(it["xbox"], 16)
        now = live_ns.get(a) or live(a, program)[0]
        done = now == it["name"]
        pending += not done
        lines.append(f"| {'yes' if done else ''} | {it['xbox']} | {now} | {it['name']} |")
    path = batch_path.replace(".json", "-namespaces.md")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{path}: {pending} still to move")


def pending_moves():
    """Every batch's namespace moves that live Ghidra doesn't show yet, for ghidra/NightfireNamespaces.py to
    apply: results/namespace-moves.json. Only entries whose function still has the batch's bare name go in."""
    import glob

    moves = []
    live_ns, bare = {}, {}
    for path in sorted(glob.glob(os.path.join(HERE, "results", "batches", "batch-*.json"))):
        if path.endswith(".log.json"):
            continue
        with open(path) as f:
            batch = json.load(f)
        program = batch.get("program", g.XBOX)
        if program not in live_ns:
            live_ns[program] = g.qualified_names(program)
            bare[program] = dict(g.functions(program))
        for it in batch["items"]:
            if "::" not in it["name"] or not it.get("rename", True):
                continue
            a = int(it["xbox"], 16)
            if live_ns[program].get(a) == it["name"]:
                continue
            if bare[program].get(a) != names.bare(it["name"]):
                print(f"  skipped {program} {it['xbox']}: now {bare[program].get(a)}, not {names.bare(it['name'])}")
                continue
            moves.append({"program": program, "address": it["xbox"], "name": it["name"], "batch": batch["batch"]})
    out = os.path.join(HERE, "results", "namespace-moves.json")
    with open(out, "w") as f:
        json.dump({"generated": time.ctime(), "moves": moves}, f, indent=1)
    print(f"{out}: {len(moves)} moves")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--pending-namespaces" in sys.argv:
        pending_moves()
    elif "--namespaces" in sys.argv:
        namespaces(args[0])
    elif "--undo" in sys.argv:
        undo(args[0], "--apply" in sys.argv)
    else:
        run(args[0], "--apply" in sys.argv)
