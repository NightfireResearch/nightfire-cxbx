"""Imports a level exported by export_level.py into Blender, set up for looking around it.

Run it from Blender's Scripting workspace (edit PATH and TYPES_DIR below), or from a shell:
  blender --python blender_import_level.py -- 07000001.glb

On top of Blender's glTF importer it:
- gives every placement property a tooltip (Object Properties > Custom Properties: hover over a field) from the
  placement type tables (tools/level/types_*.json), and switch-channel fields a 0..255 range and a note saying
  whether the object reads or writes the channel;
- builds a "Switch channels" collection with one child collection per channel ("ch 011 (1 writes, 3 read)")
  holding every object that uses it - objects stay where they are too (a Blender object can be in several
  collections), so isolating a channel's collection shows what triggers what;
- hides the objects the game never draws (custom property "hidden": glist flag 0x10 or 0x8000 - editor markers
  such as the red boxes of Destroy volumes). The file marks them with KHR_node_visibility too, which Blender's
  importer does not apply (5.1);
- sets the scene to 60 fps (the rate of the game's animations) and the viewport's clip end to 5 km.
"""
import glob
import json
import os
import re
import sys

import bpy

PATH = r''        # or set the .glb here when running from the Scripting workspace
TYPES_DIR = r''   # tools/level (found next to this script when run from a shell)

if '--' in sys.argv and len(sys.argv) > sys.argv.index('--') + 1:
    PATH = sys.argv[sys.argv.index('--') + 1]
if not TYPES_DIR:
    here = os.path.dirname(os.path.abspath(__file__)) if '__file__' in globals() else ''
    TYPES_DIR = here if glob.glob(os.path.join(here, 'types_*.json')) else ''

META = {
    'placementType': 'The placement type (Place_* in src/action/engine/parsemap.h) and its number.',
    'placementIndex': 'The record number in the map file\'s placement block (0x1a).',
    'glist': 'The entity (model) the placement points at; for many types a marker the game does not draw.',
    'glistHash': 'The entity\'s hashcode (0x02xxxxxx), when it has one.',
    'pathIndex': 'The path (map block 0x19) this object follows: see paths/object paths/path N.',
    'pathFrames': 'How many of the path\'s frames the object uses.',
    'pathRate': 'The path playback rate field (placement +0x42).',
    'hidden': 'The game never draws this glist (entity flag 0x10 or 0x8000): an editor marker.',
}


def load_types():
    types = {}
    for fn in glob.glob(os.path.join(TYPES_DIR, 'types_*.json')) if TYPES_DIR else []:
        for tid, t in json.load(open(fn)).items():
            if not tid.startswith('_') and isinstance(t, dict):
                types[int(tid)] = t
    return types


def describe(obj, types):
    """tooltips (and channel ranges) for one object's placement properties"""
    m = re.search(r'\((\d+)\)$', str(obj.get('placementType', '')))
    if not m:
        return 0
    t = types.get(int(m.group(1)), {})
    fields = {f['name']: f for f in t.get('fields', [])}
    done = 0
    for name in list(obj.keys()):
        if name.startswith('_'):
            continue
        f = fields.get(name) or fields.get(re.sub(r'^key\d+_', '', name))
        if f is not None:
            text = 'key %d (level_tag +%s). %s' % (f['key'], f.get('offset', '?'), f.get('note', ''))
            if f.get('type') == 'switch':
                role = {'in': 'READS this switch channel (waits on / reacts to it)',
                        'out': 'WRITES this switch channel (sets it)',
                        'inout': 'reads AND writes this switch channel'}.get(f.get('channelRole'), 'uses this switch channel')
                text = '%s. %s' % (role, text)
        elif name in META:
            text = META[name]
        else:
            continue
        try:
            ui = obj.id_properties_ui(name)
            if isinstance(obj[name], int) and f is not None and f.get('type') == 'switch':
                ui.update(description=text[:1000], min=0, max=255, soft_min=0, soft_max=255)
            else:
                ui.update(description=text[:1000])
            done += 1
        except TypeError:
            pass     # arrays and strings without a UI of that kind
    return done


def channel_collections(objs):
    users = {}
    for o in objs:
        for key, role in (('_channelsOut', 'w'), ('_channelsIn', 'r'), ('_channelsUsed', 'u')):
            for c in o.get(key, []) or []:
                users.setdefault(int(c), []).append((o, role))
    if not users:
        return 0
    top = bpy.data.collections.new('Switch channels')
    bpy.context.scene.collection.children.link(top)
    for c in sorted(users):
        lst = users[c]
        w = sum(1 for _, r in lst if r == 'w')
        r = sum(1 for _, r_ in lst if r_ == 'r')
        u = len(lst) - w - r
        label = 'ch %03d (%d write%s, %d read%s' % (c, w, '' if w == 1 else 's', r, '' if r == 1 else '')
        label += ', %d either)' % u if u else ')'
        col = bpy.data.collections.new(label)
        top.children.link(col)
        for o, _ in lst:
            if col.objects.get(o.name) is None:
                col.objects.link(o)
    return len(users)


scene = bpy.context.scene
scene.render.fps = 60
before = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=PATH)
new = [o for o in bpy.data.objects if o not in before]

types = load_types()
tips = sum(describe(o, types) for o in new) if types else 0
channels = channel_collections(new)
hidden = 0
for o in new:
    if o.get('hidden'):
        o.hide_set(True)
        o.hide_render = True
        hidden += 1
for area in getattr(bpy.context.screen, 'areas', None) or []:
    if area.type == 'VIEW_3D':
        for sp in area.spaces:
            if sp.type == 'VIEW_3D':
                sp.clip_end = 5000
print('imported %s: %d objects, %d property tooltips%s, %d switch channels, %d hidden (never drawn by the game)' % (
    PATH, len(new), tips, '' if types else ' (types_*.json not found: set TYPES_DIR)', channels, hidden))
