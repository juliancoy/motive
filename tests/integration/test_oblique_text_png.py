#!/usr/bin/env python3
"""Headless text render test: generate oblique text PNG and validate output geometry."""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import zlib
from pathlib import Path


PNG_SIG = b"\x89PNG\r\n\x1a\n"


def paeth_predictor(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png_rgba8(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if len(data) < 8 or data[:8] != PNG_SIG:
        raise RuntimeError("invalid PNG signature")

    pos = 8
    width = height = None
    bit_depth = color_type = interlace = None
    idat = bytearray()

    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        ctype = data[pos + 4 : pos + 8]
        cdata = data[pos + 8 : pos + 8 + length]
        pos += 12 + length

        if ctype == b"IHDR":
            width, height, bit_depth, color_type, _comp, _filt, interlace = struct.unpack(">IIBBBBB", cdata
            )
        elif ctype == b"IDAT":
            idat.extend(cdata)
        elif ctype == b"IEND":
            break

    if width is None or height is None:
        raise RuntimeError("PNG missing IHDR")
    if bit_depth != 8 or color_type != 6 or interlace != 0:
        raise RuntimeError(
            f"unsupported PNG format: bit_depth={bit_depth}, color_type={color_type}, interlace={interlace}"
        )

    raw = zlib.decompress(bytes(idat))
    bpp = 4
    stride = width * bpp
    expected = height * (1 + stride)
    if len(raw) != expected:
        raise RuntimeError(f"unexpected decompressed size {len(raw)} != {expected}")

    out = bytearray(height * stride)
    src = 0
    dst = 0
    prev = bytearray(stride)

    for _y in range(height):
        f = raw[src]
        src += 1
        scan = bytearray(raw[src : src + stride])
        src += stride

        if f == 0:
            pass
        elif f == 1:
            for i in range(stride):
                left = scan[i - bpp] if i >= bpp else 0
                scan[i] = (scan[i] + left) & 0xFF
        elif f == 2:
            for i in range(stride):
                scan[i] = (scan[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                left = scan[i - bpp] if i >= bpp else 0
                up = prev[i]
                scan[i] = (scan[i] + ((left + up) // 2)) & 0xFF
        elif f == 4:
            for i in range(stride):
                left = scan[i - bpp] if i >= bpp else 0
                up = prev[i]
                upleft = prev[i - bpp] if i >= bpp else 0
                scan[i] = (scan[i] + paeth_predictor(left, up, upleft)) & 0xFF
        else:
            raise RuntimeError(f"unsupported PNG filter {f}")

        out[dst : dst + stride] = scan
        prev = scan
        dst += stride

    return width, height, bytes(out)


def rgba_at(buf: bytes, width: int, x: int, y: int) -> tuple[int, int, int, int]:
    i = (y * width + x) * 4
    return buf[i], buf[i + 1], buf[i + 2], buf[i + 3]


def color_distance_sq(a: tuple[int, int, int], b: tuple[int, int, int]) -> int:
    dr = int(a[0]) - int(b[0])
    dg = int(a[1]) - int(b[1])
    db = int(a[2]) - int(b[2])
    return dr * dr + dg * dg + db * db


def analyze_oblique(width: int, height: int, buf: bytes) -> dict:
    min_x, min_y = width, height
    max_x, max_y = -1, -1
    text_pixels = 0
    sum_r = 0
    sum_g = 0
    sum_b = 0
    bg = rgba_at(buf, width, 0, 0)
    bg_rgb = (bg[0], bg[1], bg[2])

    for y in range(height):
        row_off = y * width * 4
        for x in range(width):
            i = row_off + x * 4
            r, g, b = buf[i], buf[i + 1], buf[i + 2]
            lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
            chroma = max(r, g, b) - min(r, g, b)
            # Isolate bright near-neutral glyph pixels; robust to gradient backgrounds.
            if lum >= 212.0 and chroma <= 24:
                text_pixels += 1
                sum_r += r
                sum_g += g
                sum_b += b
                min_x = min(min_x, x)
                min_y = min(min_y, y)
                max_x = max(max_x, x)
                max_y = max(max_y, y)

    if text_pixels == 0:
        raise AssertionError("rendered image has no visible text pixels")

    bbox_w = max_x - min_x + 1
    bbox_h = max_y - min_y + 1

    # Measure left edge at top and bottom scanline bands of the text bbox.
    band_h = max(1, bbox_h // 8)

    def left_edge(y0: int, y1: int) -> float:
        xs = []
        for yy in range(y0, y1):
            row_off = yy * width * 4
            for xx in range(min_x, max_x + 1):
                i = row_off + xx * 4
                rgb = (buf[i], buf[i + 1], buf[i + 2])
                r, g, b = rgb
                lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
                chroma = max(r, g, b) - min(r, g, b)
                if not (lum >= 212.0 and chroma <= 24):
                    continue
                xs.append(xx)
                break
        return float(sum(xs)) / float(len(xs)) if xs else float(min_x)

    top_left = left_edge(min_y, min_y + band_h)
    bottom_left = left_edge(max_y - band_h + 1, max_y + 1)
    shear_delta = bottom_left - top_left

    # Background is flat per generator; verify we still retain a mostly uniform background region.
    corner = rgba_at(buf, width, 0, 0)
    avg_fg = (
        int(sum_r / text_pixels),
        int(sum_g / text_pixels),
        int(sum_b / text_pixels),
    )

    return {
        "text_pixels": text_pixels,
        "bbox_w": bbox_w,
        "bbox_h": bbox_h,
        "min_x": min_x,
        "min_y": min_y,
        "max_x": max_x,
        "max_y": max_y,
        "shear_delta": shear_delta,
        "corner_rgba": corner,
        "avg_fg_rgb": avg_fg,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate and validate oblique text PNG")
    parser.add_argument("--bin", default="./build/text_oblique_png", help="Path to renderer binary")
    parser.add_argument("--out", default="/tmp/motive_oblique_text_test.png", help="Output PNG path")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    bin_path = (repo_root / args.bin).resolve() if args.bin.startswith(".") else Path(args.bin).resolve()
    out_path = Path(args.out)

    if not bin_path.exists():
        raise FileNotFoundError(f"renderer binary not found: {bin_path}")

    cmd = [
        str(bin_path),
        "--text",
        "What",
        "--out",
        str(out_path),
        "--pixel-height",
        "104",
        "--scale",
        "2.0",
        "--shear-x",
        "-0.65",
        "--rotate-deg",
        "-20",
        "--canvas-width",
        "1600",
        "--canvas-height",
        "900",
    ]
    subprocess.check_call(cmd, cwd=str(repo_root))

    width, height, pixels = decode_png_rgba8(out_path)
    stats = analyze_oblique(width, height, pixels)

    if width < 1200 or height < 700:
        raise AssertionError(f"output resolution too low: {width}x{height}")
    if stats["text_pixels"] < 12000:
        raise AssertionError(f"insufficient text coverage: {stats}")
    if stats["text_pixels"] > 220000:
        raise AssertionError(f"text flood detected: {stats}")
    if stats["bbox_w"] < 220 or stats["bbox_h"] < 120:
        raise AssertionError(f"text bbox unexpectedly small: {stats}")
    if stats["min_x"] <= 16 or stats["min_y"] <= 16:
        raise AssertionError(f"text too close to top/left edge: {stats}")
    if stats["max_x"] >= width - 16 or stats["max_y"] >= height - 16:
        raise AssertionError(f"text too close to bottom/right edge: {stats}")

    # Negative shear-x means lower rows should be shifted left relative to upper rows.
    if stats["shear_delta"] > -18.0:
        raise AssertionError(f"text is not oblique enough (shear delta): {stats}")

    # Keep background expectation stable (helps catch accidental transparent outputs).
    corner = stats["corner_rgba"]
    expected_a = 255
    if corner[3] != expected_a:
        raise AssertionError(f"unexpected background alpha: {stats}")

    # Ensure text remains high-contrast against background.
    if color_distance_sq(stats["avg_fg_rgb"], corner[:3]) < 14000:
        raise AssertionError(f"insufficient text/background contrast: {stats}")

    print(f"Oblique text PNG test passed: {out_path} :: {stats}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001
        print(f"FAILED: {exc}")
        sys.exit(1)
