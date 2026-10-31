"""Blender helpers for the scenes export_level_diff.py writes (imported by blender_import_level.py and
blender_render_level.py; runs inside Blender).

    diff_roots(objs)          the "diff_<level>" root empties among the imported objects
    diff_collections(root)    a collection per difference group (unchanged, added, removed, moved or changed,
                              changed geometry) and per sub-group that is not an export_level.py group, holding
                              those groups' objects
    show_colours()            each material's viewport colour set from its base colour, so the solid view and
                              Workbench's material colours show the groups' colours
"""
import bpy

EXPORT_GROUPS = ('rooms', 'static', 'objects', 'characters', 'volumes', 'markers', 'lights', 'sky', 'paths',
                 'collision', 'portals', 'moves')


def diff_roots(objs):
    return [o for o in objs if o.name.startswith('diff_') and o.parent is None]


def descendants(o):
    out = [o]
    for c in o.children:
        out += descendants(c)
    return out


def move_to(objs, col):
    for o in objs:
        for c in list(o.users_collection):
            if not c.name.startswith('ch ') and c.name != 'Switch channels':
                c.objects.unlink(o)
        col.objects.link(o)


def diff_collections(root):
    top = bpy.data.collections.new(root.name)
    bpy.context.scene.collection.children.link(top)
    move_to([root], top)
    made = 0
    for g in sorted(root.children, key=lambda o: o.name):
        col = bpy.data.collections.new('%s: %s' % (root.name[5:], g.name))
        top.children.link(col)
        made += 1
        rest = [g]
        for sub in sorted(g.children, key=lambda o: o.name):
            if sub.name.split('.')[0] in EXPORT_GROUPS:
                rest += descendants(sub)
            else:
                sc = bpy.data.collections.new('%s: %s/%s' % (root.name[5:], g.name, sub.name))
                col.children.link(sc)
                move_to(descendants(sub), sc)
                made += 1
        move_to(rest, col)
    return made


def show_colours():
    for m in bpy.data.materials:
        bsdf = m.node_tree.nodes.get('Principled BSDF') if m.use_nodes and m.node_tree else None
        if bsdf is None:
            continue
        c = list(bsdf.inputs['Base Color'].default_value)
        a = bsdf.inputs['Alpha'].default_value
        m.diffuse_color = (c[0], c[1], c[2], a)
