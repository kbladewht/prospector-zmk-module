import argparse
import math
import re
from pathlib import Path


def parse_array(source, name):
    match = re.search(r"\b" + name + r"\[\]\s*=\s*\{(.*?)\n\};", source, re.S)
    if match is None:
        raise ValueError(f"Could not find {name}")
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", match.group(1), flags=re.S)
    return body


def parse_font(path, scale):
    source = Path(path).read_text(encoding="utf-8")
    bitmap = [
        int(value, 16)
        for value in re.findall(
            r"0x([0-9a-fA-F]+)", parse_array(source, "glyph_bitmap")
        )
    ]
    descriptions = []
    for entry in re.findall(r"\{([^{}]+)\}", parse_array(source, "glyph_dsc")):
        fields = {
            key: int(value)
            for key, value in re.findall(
                r"\.(bitmap_index|adv_w|box_w|box_h|ofs_x|ofs_y)\s*=\s*(-?\d+)", entry
            )
        }
        if len(fields) == 6:
            descriptions.append(fields)

    # Printable ASCII through underscore, matching the renderer's uppercase mapping.
    codes = list(range(32, 96))
    packed = []
    glyphs = []

    for code in codes:
        glyph_id = 1 + code - 32
        desc = descriptions[glyph_id]
        width, height = desc["box_w"], desc["box_h"]
        target_width = round(width * scale)
        target_height = round(height * scale)
        pixels = []

        if width and height:
            source_pixels = []
            for index in range(width * height):
                value = bitmap[desc["bitmap_index"] + index // 2]
                source_pixels.append(
                    (value >> 4) & 15 if index % 2 == 0 else value & 15
                )

            for target_y in range(target_height):
                y0 = target_y * height / target_height
                y1 = (target_y + 1) * height / target_height
                for target_x in range(target_width):
                    x0 = target_x * width / target_width
                    x1 = (target_x + 1) * width / target_width
                    weighted = 0.0
                    area = 0.0
                    for source_y in range(math.floor(y0), math.ceil(y1)):
                        overlap_y = max(0.0, min(y1, source_y + 1) - max(y0, source_y))
                        for source_x in range(math.floor(x0), math.ceil(x1)):
                            overlap_x = max(
                                0.0, min(x1, source_x + 1) - max(x0, source_x)
                            )
                            weight = overlap_x * overlap_y
                            weighted += (
                                source_pixels[source_y * width + source_x] * weight
                            )
                            area += weight
                    pixels.append(
                        max(0, min(15, round(weighted / area))) if area else 0
                    )

        if len(packed) % 2:
            packed.append(0)
        bitmap_offset = len(packed)
        for index in range(0, len(pixels), 2):
            high = pixels[index]
            low = pixels[index + 1] if index + 1 < len(pixels) else 0
            packed.append((high << 4) | low)

        glyphs.append(
            (
                bitmap_offset,
                target_width,
                target_height,
                max(1, round(desc["adv_w"] * scale / 16)),
                round(desc["ofs_x"] * scale),
                round(desc["ofs_y"] * scale),
            )
        )

    return packed, glyphs


def emit_font(output, name, packed, glyphs):
    output.write(f"static const uint8_t {name}_bitmap[] = {{\n")
    for index in range(0, len(packed), 16):
        values = ", ".join(f"0x{value:02x}" for value in packed[index : index + 16])
        output.write(f"    {values},\n")
    output.write("};\n\n")

    output.write(f"static const struct screen_font_glyph {name}_glyphs[] = {{\n")
    for glyph in glyphs:
        output.write("    {%d, %d, %d, %d, %d, %d},\n" % glyph)
    output.write("};\n\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("font20")
    parser.add_argument("font28")
    parser.add_argument("output")
    args = parser.parse_args()

    font20 = parse_font(args.font20, 1.0)
    font28 = parse_font(args.font28, 0.9)
    with Path(args.output).open("w", encoding="ascii", newline="\n") as output:
        output.write("#pragma once\n\n#include <stdint.h>\n\n")
        output.write(
            "struct screen_font_glyph { uint16_t bitmap_offset; uint8_t width, height, advance; "
            "int8_t offset_x, offset_y; };\n\n"
        )
        emit_font(output, "screen_font20", *font20)
        emit_font(output, "screen_font28", *font28)


if __name__ == "__main__":
    main()
