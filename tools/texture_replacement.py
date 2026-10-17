#!/usr/bin/env python3
# ---------------------------------------------------------------------------------------------------------------
# Making replacement textures for src/common/gfx/textureReplace.cpp.
#
# The backend names every texture by a hash of the game's data for it. DumpTextures=1 in settings.ini writes
# each one the game uses to textures_dump/<hash>.dds (and a font's glyph boxes to <hash>.font.json); a file
# textures/<hash>.dds beside the executable replaces it, at any size, with its own mip chain.
#
#   python3 tools/texture_replacement.py to-png build/linux/textures_dump/*.dds
#       the dumps as PNGs beside them, for looking at or painting over
#
#   python3 tools/texture_replacement.py to-dds edited.png build/linux/textures/<hash>.dds
#       a PNG (any size) as a replacement: 32-bit ARGB with a full mip chain
#
#   python3 tools/texture_replacement.py font build/linux/textures_dump/<hash>.font.json ~/.local/share/fonts/x.ttf
#           --scale 8 --out build/linux/textures/<hash>.dds --preview preview.png
#       a TrueType/OpenType font rendered into the game font's own glyph boxes, <scale> times the size
#
# The game positions every character by the box it has for it in the sheet, draws the box's contents
# stretched to the box, and advances by the box's width. So each new glyph is drawn centred on the original
# glyph's ink in its box, at the original's height - so baselines, letter heights and spacing stay consistent -
# and one that would be wider than the box is squeezed to fit. A character the font does not have keeps the
# original glyph, upscaled.
#
# Needs Pillow; fontTools too for the font subcommand (to tell which characters the font really has).
# ---------------------------------------------------------------------------------------------------------------

import argparse
import json
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

DDS_MAGIC = b"DDS "
DDPF_ALPHAPIXELS, DDPF_FOURCC, DDPF_RGB = 0x1, 0x4, 0x40

# The game's fonts are laid out by single-byte character codes that are Windows-1252 except where the game's
# own font tool put something else in the slot (seen in action_font_3). The sheets differ: action_font_1 and 2
# have ligatures and a box instead (--char 0x5E=ﬃ --char 0x7C=ﬄ --char 0x7E=ﬀ --char 0x85=€ --char 0x8C=).
GAME_ENCODING_EXCEPTIONS = {
    0x8C: "\u2022",   # a solid dot, where Windows-1252 has the OE ligature
    0xA7: "\u0152",   # the OE ligature, where Windows-1252 has the section sign
}


def game_character(code, encoding, overrides):
    """The character a glyph code stands for."""
    if code in overrides:
        return overrides[code]
    if encoding in ("nightfire", "cp1252") and code in GAME_ENCODING_EXCEPTIONS:
        return GAME_ENCODING_EXCEPTIONS[code]
    return bytes([code]).decode("cp1252", errors="replace")


def read_dds(path):
    """Level 0 of an uncompressed 32-bit ARGB DDS, as an RGBA image."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != DDS_MAGIC or len(data) < 128:
        raise ValueError(f"{path}: not a DDS file")
    height, width = struct.unpack_from("<II", data, 12)
    pf_flags, fourcc, bits, rmask, gmask, bmask, amask = struct.unpack_from("<II5I", data, 80)
    if pf_flags & DDPF_FOURCC or bits != 32 or (rmask, gmask, bmask) != (0x00FF0000, 0x0000FF00, 0x000000FF):
        raise ValueError(f"{path}: only 32-bit ARGB DDS files are read here")
    return Image.frombuffer("RGBA", (width, height), data[128:128 + width * height * 4], "raw", "BGRA", 0, 1).copy()


def write_dds(image, path, mips=True):
    """An RGBA image as 32-bit ARGB DDS, with its mip chain down to 1x1 unless mips is False."""
    image = image.convert("RGBA")
    levels = [image]
    while mips and (levels[-1].width > 1 or levels[-1].height > 1):
        prev = levels[-1]
        levels.append(prev.resize((max(1, prev.width // 2), max(1, prev.height // 2)), Image.Resampling.BOX))
    header = bytearray(128)
    header[0:4] = DDS_MAGIC
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x8 | (0x20000 if len(levels) > 1 else 0)   # ... pitch, mip count
    struct.pack_into("<IIIIIII", header, 4, 124, flags, image.height, image.width, image.width * 4, 0, len(levels))
    struct.pack_into("<II5I", header, 76, 32, DDPF_RGB | DDPF_ALPHAPIXELS, 0, 32,
                     0x00FF0000, 0x0000FF00, 0x000000FF)
    struct.pack_into("<I", header, 104, 0xFF000000)   # alpha mask
    caps = 0x1000 | ((0x8 | 0x400000) if len(levels) > 1 else 0)   # texture (| complex | mipmap)
    struct.pack_into("<I", header, 108, caps)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(header)
        for level in levels:
            f.write(level.tobytes("raw", "BGRA"))


def cmd_to_png(args):
    for path in args.files:
        out = os.path.splitext(path)[0] + ".png"
        read_dds(path).save(out)
        print(f"wrote {out}")


def cmd_to_dds(args):
    write_dds(Image.open(args.png), args.out, mips=not args.no_mips)
    print(f"wrote {args.out}")


def font_characters(font_path):
    """The characters a font file maps to glyphs."""
    from fontTools.ttLib import TTFont
    font = TTFont(font_path, fontNumber=0, lazy=True)
    return set(chr(c) for c in font.getBestCmap().keys())


def ink_box(alpha, box):
    """The bounding box of the non-transparent pixels of a box in an alpha image, or None."""
    region = alpha.crop(box)
    bbox = region.point(lambda a: 255 if a > 24 else 0).getbbox()
    if bbox is None:
        return None
    return (box[0] + bbox[0], box[1] + bbox[1], box[0] + bbox[2], box[1] + bbox[3])


def cmd_font(args):
    with open(args.metrics) as f:
        metrics = json.load(f)
    scale = args.scale
    width, height = metrics["width"], metrics["height"]
    encoding = metrics.get("encoding", "nightfire")
    overrides = {}
    for item in args.char or []:
        code, _, ch = item.partition("=")
        overrides[int(code, 0)] = ch

    # The original sheet, dumped beside the metrics: where each glyph's ink sits inside its box, and what to
    # fall back to for a character the new font lacks.
    original_path = args.original or args.metrics.replace(".font.json", ".dds")
    original = read_dds(original_path)
    original_alpha = original.getchannel("A")

    have = font_characters(args.ttf)
    render_size = args.render_size
    ttf = ImageFont.truetype(args.ttf, render_size)

    alpha = Image.new("L", (width * scale, height * scale), 0)
    replaced, kept, suspicious, overlapping = 0, [], [], []
    drawn = []   # the boxes filled so far
    for g in metrics["glyphs"]:
        if g["w"] == 0 or g["h"] == 0:
            continue
        box = (g["u"], g["v"], g["u"] + g["w"], g["v"] + g["h"])
        # A box inside another glyph's (some sheets point º into the right half of ®) has no glyph of its own:
        # drawing one there would paint over its neighbour, so the first glyph keeps the space.
        if any(box[0] < d[2] and d[0] < box[2] and box[1] < d[3] and d[1] < box[3] for d in drawn):
            overlapping.append(f"0x{g['code']:02X}")
            continue
        drawn.append(box)
        ink = ink_box(original_alpha, box)
        if ink is None:
            continue   # a space, or an empty cell
        ch = game_character(g["code"], encoding, overrides)
        target = tuple(c * scale for c in ink)
        cell = tuple(c * scale for c in box)
        target_w, target_h = target[2] - target[0], target[3] - target[1]

        if not ch or any(c not in have for c in ch) or ch == "�":   # several characters: drawn as one, e.g. ffi
            # Not in the font: the original glyph, upscaled, in its own place.
            glyph = original_alpha.crop(ink).resize((target_w, target_h), Image.Resampling.LANCZOS)
            alpha.paste(glyph, target[:2])
            kept.append(ch)
            continue

        # The new glyph at a large size, cropped to its ink.
        canvas = Image.new("L", (render_size * 3, render_size * 2), 0)
        ImageDraw.Draw(canvas).text((render_size // 2, render_size // 2), ch, font=ttf, fill=255)
        bbox = canvas.getbbox()
        if bbox is None:
            continue
        glyph = canvas.crop(bbox)

        # A glyph whose shape is nothing like the original's is most likely the wrong character for the slot
        # (see GAME_ENCODING_EXCEPTIONS): said, so it can be put right with --char.
        original_aspect = target_w / target_h
        new_aspect = glyph.width / glyph.height
        if max(original_aspect, new_aspect) / max(min(original_aspect, new_aspect), 1e-6) > 3.0:
            suspicious.append(f"0x{g['code']:02X} {ch!r} (the original is {target_w}x{target_h}, the new one "
                              f"{glyph.width}x{glyph.height} before fitting)")

        # To the original ink's height; narrower if it would overrun the box.
        new_h = target_h
        new_w = max(1, round(glyph.width * target_h / glyph.height))
        if new_w > cell[2] - cell[0]:
            new_w = cell[2] - cell[0]
        glyph = glyph.resize((new_w, new_h), Image.Resampling.LANCZOS)
        # Centred on the original ink, inside the box.
        x = target[0] + (target_w - new_w) // 2
        x = min(max(x, cell[0]), cell[2] - new_w)
        alpha.paste(glyph, (x, target[1]))
        replaced += 1

    # White, with the glyphs in alpha: what the original sheets hold, and what the game's combiners expect.
    sheet = Image.merge("RGBA", (Image.new("L", alpha.size, 255),) * 3 + (alpha,))
    write_dds(sheet, args.out)
    print(f"wrote {args.out}: {sheet.width}x{sheet.height}, {replaced} glyphs from {os.path.basename(args.ttf)}"
          + (f", {len(kept)} kept from the original ({''.join(kept)})" if kept else ""))
    if overlapping:
        print(f"  left as they were: {', '.join(overlapping)} (their boxes overlap another glyph's)")
    for line in suspicious:
        print(f"  check: {line} - use --char CODE=CHARACTER if it is the wrong character")

    if args.preview:
        backdrop = Image.new("RGBA", sheet.size, (24, 24, 28, 255))
        backdrop.alpha_composite(sheet)
        backdrop.convert("RGB").save(args.preview)
        print(f"wrote {args.preview}")


def main():
    parser = argparse.ArgumentParser(description="Make replacement textures for the D3D9 backend.")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("to-png", help="dumped DDS files to PNGs beside them")
    p.add_argument("files", nargs="+")
    p.set_defaults(run=cmd_to_png)

    p = sub.add_parser("to-dds", help="a PNG to a replacement DDS with mipmaps")
    p.add_argument("png")
    p.add_argument("out")
    p.add_argument("--no-mips", action="store_true", help="level 0 only")
    p.set_defaults(run=cmd_to_dds)

    p = sub.add_parser("font", help="a TrueType/OpenType font rendered into a game font's glyph boxes")
    p.add_argument("metrics", help="textures_dump/<hash>.font.json")
    p.add_argument("ttf", help="the font file")
    p.add_argument("--out", required=True, help="textures/<hash>.dds")
    p.add_argument("--scale", type=int, default=8, help="how many times the original sheet's size (default 8)")
    p.add_argument("--original", help="the dumped sheet, if not <hash>.dds beside the metrics")
    p.add_argument("--render-size", type=int, default=256, help="pixel size glyphs are rendered at before fitting")
    p.add_argument("--preview", help="also write the sheet over a dark background as this PNG")
    p.add_argument("--char", action="append", metavar="CODE=CHARACTER",
                   help="the character for a glyph code, where the game's differs (e.g. --char 0x8C=•), or nothing to keep the original glyph (--char 0x8C=), or several to draw together where the font has no ligature (--char 0x5E=ffi); repeatable")
    p.set_defaults(run=cmd_font)

    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    sys.exit(main())
