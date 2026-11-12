"""Apply the Chinese UI catalog and generate the embedded Fusion Pixel subset."""
from pathlib import Path
import argparse
import os
import re
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
UI_FILES = ["render.c", "setup.c", "update.c", "advanced.c", "notice.c", "boot_show.c", "backgrounds.c", "progression.c", "battery.c", "../firmware/main/main.c"]
TOKENS = re.compile(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/')
TABLES = re.compile(r'\b(?:STAGE_NAMES|SHAPE_NAMES|FINS_NAMES|PATTERN_NAMES|GENE_NAMES|GOAL_NAMES|MS_NAMES|TMS_NAMES)\[')


def catalog():
    pairs = {}
    for line in (ROOT / "localization/zh_CN.tsv").read_text(encoding="utf-8").splitlines():
        english, chinese = line.split("\t", 1)
        conversions = r"%(?:[-+ #0]*\d*(?:\.\d+)?[hlzjtL]*[diuoxXfFeEgGcs]|%)"
        if re.findall(conversions, english) != re.findall(conversions, chinese):
            raise ValueError(f"Printf arguments changed in {english!r}")
        pairs[english] = chinese
    return pairs


def apply_catalog(pairs):
    for name in UI_FILES:
        file = ROOT / "common" / name
        source = file.read_text(encoding="utf-8")
        file_pairs = pairs
        if name == "../firmware/main/main.c":
            file_pairs = {key: value for key, value in pairs.items() if key.startswith(("FISH TANK", "FISH THAT", "MORE KINDS"))}
        def replace(match):
            token = match[0]
            if token.startswith('"') and token[1:-1] in file_pairs:
                return '"' + file_pairs[token[1:-1]] + '"'
            return token
        source = TOKENS.sub(replace, source)
        # Names from these tables also feed the English model and command-line
        # interfaces. Localize only their presentation in the UI files.
        code_only = TOKENS.sub(lambda match: " " * len(match[0]), source)
        positions = list(TABLES.finditer(code_only))
        for match in reversed(positions):
            if source[max(0, match.start() - 12):match.start()] == "ui_zh_label(":
                continue
            end = match.end()
            depth = 1
            while depth:
                depth += (source[end] == "[") - (source[end] == "]")
                end += 1
            if source[end:].lstrip().startswith("="):
                continue
            source = source[:match.start()] + "ui_zh_label(" + source[match.start():end] + ")" + source[end:]
        if '#include "ui_text.h"' not in source:
            source = '#include "ui_text.h"\n' + source
        if name == "render.c":
            source = re.sub(r'"(?:S|ES)"', '""', source)
        if file.read_text(encoding="utf-8") != source:
            file.write_text(source, encoding="utf-8", newline="\n")


def read_bdf(data, size, wanted):
    text = data.decode("utf-8")
    ascent = int(re.search(r"^FONT_ASCENT (\d+)", text, re.MULTILINE)[1])
    glyphs = {}
    for block in text.split("STARTCHAR ")[1:]:
        cp = int(re.search(r"^ENCODING (\d+)", block, re.MULTILINE)[1])
        if cp not in wanted:
            continue
        width, height, xoff, yoff = map(int, re.search(r"^BBX (.+)", block, re.MULTILINE)[1].split())
        rows = [int(row, 16) for row in block.split("BITMAP\n", 1)[1].split("ENDCHAR", 1)[0].split()]
        cell = [0] * size
        top = ascent - height - yoff
        padded_width = ((width + 7) // 8) * 8
        for y, row in enumerate(rows):
            if 0 <= top + y < size:
                for x in range(width):
                    if row & (1 << (padded_width - 1 - x)) and 0 <= x + xoff < size:
                        cell[top + y] |= 1 << (size - 1 - x - xoff)
        glyphs[cp] = cell
    return glyphs


def font_archive(font_dir, size):
    name = f"fusion-pixel-font-{size}px-monospaced-bdf-v2026.11.10.zip"
    archive = font_dir / name
    if not archive.exists():
        font_dir.mkdir(parents=True, exist_ok=True)
        url = f"https://github.com/TakWolf/fusion-pixel-font/releases/download/2026.11.10/{name}"
        with tempfile.NamedTemporaryFile(dir=font_dir, suffix=".download", delete=False) as temporary:
            temporary_name = Path(temporary.name)
        try:
            urllib.request.urlretrieve(url, temporary_name)
            with zipfile.ZipFile(temporary_name) as package:
                if package.testzip() is not None:
                    raise ValueError(f"Invalid font archive: {name}")
            os.replace(temporary_name, archive)
        finally:
            temporary_name.unlink(missing_ok=True)
    return archive


def make_font(pairs, font_dir):
    wanted = {ord(ch) for value in pairs.values() for ch in value if ord(ch) >= 128}
    wanted.update(ord(ch) for ch in (ROOT / "common/ui_text.c").read_text(encoding="utf-8") if ord(ch) >= 128)
    for file in list((ROOT / "common").glob("*.c")) + list((ROOT / "firmware/main").glob("*.c")):
        if file.name != "zh_font.c":
            for token in TOKENS.finditer(file.read_text(encoding="utf-8")):
                if token[0].startswith('"'):
                    wanted.update(ord(ch) for ch in token[0] if ord(ch) >= 128)
    wanted.update(range(32, 127))
    required = wanted.copy()
    # Include available GB2312 glyphs for Chinese Wi-Fi names as well.
    for high in range(0xb0, 0xf8):
        for low in range(0xa1, 0xff):
            try:
                wanted.add(ord(bytes([high, low]).decode("gb2312")))
            except UnicodeDecodeError:
                pass
    fonts = {}
    destination = ROOT / "assets/fonts/fusion-pixel"
    destination.mkdir(parents=True, exist_ok=True)
    for size in (8, 12):
        archive = font_archive(font_dir, size)
        with zipfile.ZipFile(archive) as package:
            fonts[size] = read_bdf(package.read(f"fusion-pixel-{size}px-monospaced-zh_hans.bdf"), size, wanted)
            for name in package.namelist():
                if name.startswith("LICENSES/") or name == "OFL.txt":
                    target = destination / f"{size}px" / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(package.read(name))
    missing = required - fonts[12].keys()
    if missing:
        raise ValueError(f"Missing Chinese glyphs: {''.join(map(chr, sorted(missing)))}")
    wanted.intersection_update(fonts[12])
    lines = ['/* Generated from Fusion Pixel 2026.11.10, SIL OFL 1.1. See assets/fonts/fusion-pixel/. */',
             '#include "ui_text.h"', 'static const ui_zh_glyph_t GLYPHS[] = {']
    for cp in sorted(wanted):
        small = fonts[8].get(cp)
        if small is None:
            large = fonts[12][cp]
            small = [sum(((large[y * 12 // 8] >> (11 - x * 12 // 8)) & 1) << (7 - x) for x in range(8)) for y in range(8)]
        a = ",".join(f"0x{row:02x}" for row in small)
        b = ",".join(f"0x{row:03x}" for row in fonts[12][cp])
        lines.append(f"    {{0x{cp:04x}, {{{a}}}, {{{b}}}}},")
    lines += ['};', 'const ui_zh_glyph_t *ui_zh_find(uint32_t codepoint) {',
              '    if (codepoint >= 32 && codepoint <= 126) return &GLYPHS[codepoint - 32];',
              '    size_t left = 0, right = sizeof GLYPHS / sizeof GLYPHS[0];',
              '    while (left < right) {', '        size_t middle = left + (right - left) / 2;',
              '        if (GLYPHS[middle].codepoint < codepoint) left = middle + 1;',
              '        else right = middle;', '    }',
              '    return left < sizeof GLYPHS / sizeof GLYPHS[0] && GLYPHS[left].codepoint == codepoint ? &GLYPHS[left] : 0;', '}']
    output = ROOT / "common/zh_font.c"
    generated = "\n".join(lines) + "\n"
    if not output.exists() or output.read_text(encoding="utf-8") != generated:
        output.write_text(generated, encoding="utf-8", newline="\n")
    (destination / "README.txt").write_text(
        "Embedded GB2312 and UI subset of Fusion Pixel 2026.11.10 by TakWolf.\n"
        "https://github.com/TakWolf/fusion-pixel-font/releases/tag/2026.11.10\n"
        "The original font and upstream licenses are retained in the 8px/ and 12px/ directories.\n",
        encoding="utf-8", newline="\n")
    print(f"Generated {len(wanted)} glyphs for {len(pairs)} translated UI strings.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font-dir", type=Path, default=ROOT / ".cache/fusion-pixel/2026.11.10")
    arguments = parser.parse_args()
    translations = catalog()
    make_font(translations, arguments.font_dir)
    apply_catalog(translations)
