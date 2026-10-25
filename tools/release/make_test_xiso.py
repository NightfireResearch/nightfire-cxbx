#!/usr/bin/env python3
"""Xbox disc images (XDVDFS) for the launcher's tests, built from an extracted disc (src/launcher/Xdvdfs.h).

    python tools/release/make_test_xiso.py FOLDER OUT.iso [--redump]
        an image of everything in FOLDER: a trimmed xiso (the game partition at 0) or, with --redump, laid out as a
        full image (the partition at 0x18300000, zeros before it)
    python tools/release/make_test_xiso.py --synthetic OUT.iso [--redump] [--damage truncate|dirloop|badoffset]
        a few small made-up files in eurocom/ and driving/, for the refusal tests; --damage breaks the image: cut
        short, a directory whose table is the root's, an entry offset past its table

The trees are built as the discs' mastering tools build them: each directory's entries sorted by name ignoring
case, as a balanced binary tree, written node first with the left subtree before the right, no entry crossing a
sector (the rest of one filled with 0xFF), each table and file starting on a sector. The images are about the size
of the disc: make them where there is room, and delete them after the tests.
"""

import argparse
import os
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
REDUMP_PARTITION = 0x18300000
DIRECTORY = 0x10
NORMAL = 0x80


class Node:
    def __init__(self, name, path=None, data=None, children=None):
        self.name = name
        self.path = path        # a file on disk
        self.data = data        # or its bytes
        self.children = children
        self.sector = 0
        self.size = 0
        self.table = b""

    @property
    def directory(self):
        return self.children is not None


def from_folder(folder, name=""):
    children = []
    for entry in sorted(os.listdir(folder)):
        path = os.path.join(folder, entry)
        if os.path.isdir(path):
            children.append(from_folder(path, entry))
        else:
            node = Node(entry, path=path)
            node.size = os.path.getsize(path)
            children.append(node)
    return Node(name, children=children)


def synthetic():
    def file(name, size, seed):
        data = bytes((seed * 31 + i * 7) & 0xFF for i in range(size))
        node = Node(name, data=data)
        node.size = size
        return node
    eurocom = [file("filesys.d%02d" % i, 3000 + i * 100, i) for i in range(11)]
    eurocom.append(Node("25_fps", children=[file("07100001.xmv", 5000, 50)]))
    driving = [file("misc.viv", 9000, 60), file("bond.dat", 100, 61)]
    return Node("", children=[Node("eurocom", children=eurocom), Node("driving", children=driving),
                              file("default.xbe", 4096, 70)])


def balanced(entries):
    """(node, left, right) for entries sorted by name"""
    if not entries:
        return None
    middle = len(entries) // 2
    return (entries[middle], balanced(entries[:middle]), balanced(entries[middle + 1:]))


def entry_length(node):
    return (14 + len(node.name.encode("ascii")) + 3) & ~3


def build_table(directory):
    """The directory's table; its children's sectors must be set"""
    entries = sorted(directory.children, key=lambda n: n.name.upper())
    if not entries:
        return b"\xff" * SECTOR
    tree = balanced(entries)
    offsets = {}
    order = []
    position = 0

    def place(subtree):
        nonlocal position
        if subtree is None:
            return
        node, left, right = subtree
        length = entry_length(node)
        if position // SECTOR != (position + length - 1) // SECTOR:
            position = (position // SECTOR + 1) * SECTOR
        offsets[id(node)] = position
        order.append((node, left, right))
        position += length
        place(left)
        place(right)

    place(tree)
    table = bytearray(b"\xff" * (((position + SECTOR - 1) // SECTOR) * SECTOR))
    for node, left, right in order:
        name = node.name.encode("ascii")
        at = offsets[id(node)]
        left_offset = offsets[id(left[0])] // 4 if left else 0
        right_offset = offsets[id(right[0])] // 4 if right else 0
        record = struct.pack("<HHIIBB", left_offset, right_offset, node.sector, node.size,
                             DIRECTORY if node.directory else NORMAL, len(name)) + name
        record += b"\xff" * (entry_length(node) - len(record))
        table[at:at + len(record)] = record
    return bytes(table)


def layout(root):
    """Sectors for every table and file: the tables first, from sector 33, then the files"""
    directories, files = [], []

    def walk(node):
        if node.directory:
            directories.append(node)
            for child in node.children:
                walk(child)
        else:
            files.append(node)

    walk(root)
    # A table's size depends only on its children's names, so it is known before any sector is
    for directory in directories:
        directory.size = len(build_table(directory))
    sector = 33
    for directory in directories:
        directory.sector = sector
        sector += directory.size // SECTOR
    for node in files:
        if node.size == 0:
            node.sector = 0
            continue
        node.sector = sector
        sector += (node.size + SECTOR - 1) // SECTOR
    for directory in directories:
        directory.table = build_table(directory)
    return directories, files, sector


def write_image(root, out, partition):
    directories, files, sectors = layout(root)
    with open(out, "wb") as f:
        f.truncate(partition + sectors * SECTOR)
        descriptor = bytearray(SECTOR)
        descriptor[0:20] = MAGIC
        descriptor[20:28] = struct.pack("<II", root.sector, root.size)
        descriptor[2028:2048] = MAGIC
        f.seek(partition + 32 * SECTOR)
        f.write(descriptor)
        for directory in directories:
            f.seek(partition + directory.sector * SECTOR)
            f.write(directory.table)
        for node in files:
            if node.size == 0:
                continue
            f.seek(partition + node.sector * SECTOR)
            if node.data is not None:
                f.write(node.data)
            else:
                with open(node.path, "rb") as source:
                    while True:
                        block = source.read(1 << 22)
                        if not block:
                            break
                        f.write(block)
    return directories


def damage(out, kind, root, partition):
    if kind == "truncate":
        with open(out, "r+b") as f:
            f.truncate(os.path.getsize(out) * 6 // 10)
        return
    with open(out, "r+b") as f:
        f.seek(partition + root.sector * SECTOR)
        first = bytearray(f.read(14))
        if kind == "dirloop":      # the root's first entry, made a directory whose table is the root's
            first[4:8] = struct.pack("<I", root.sector)
            first[8:12] = struct.pack("<I", root.size)
            first[12] = DIRECTORY
        elif kind == "badoffset":  # its left subtree, past the end of the table
            first[0:2] = struct.pack("<H", 0x7FFF)
        f.seek(partition + root.sector * SECTOR)
        f.write(first)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", nargs="?")
    parser.add_argument("out")
    parser.add_argument("--redump", action="store_true", help="the partition at 0x18300000, as in a full image")
    parser.add_argument("--synthetic", action="store_true", help="small made-up files instead of a folder")
    parser.add_argument("--damage", choices=["truncate", "dirloop", "badoffset"])
    args = parser.parse_args()
    if args.synthetic == bool(args.folder):
        sys.exit("give a folder or --synthetic")
    root = synthetic() if args.synthetic else from_folder(args.folder)
    partition = REDUMP_PARTITION if args.redump else 0
    write_image(root, args.out, partition)
    if args.damage:
        damage(args.out, args.damage, root, partition)
    print(f"{args.out}: {os.path.getsize(args.out):,} bytes, partition at {partition:#x}"
          + (f", damaged ({args.damage})" if args.damage else ""))


if __name__ == "__main__":
    main()
