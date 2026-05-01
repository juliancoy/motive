#!/usr/bin/env python3
"""Headless extrusion text render test: generate extruded text PNG and validate shading cues."""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import zlib
from pathlib import Path

PNG_SIG = b"\x89PNG\r\n\x1a\n"


def decode_png_rgba8(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if data[:8] != PNG_SIG:
        raise RuntimeError("invalid PNG signature")

    pos = 8
    width = height = None
    bit_depth = color_type = interlace = None
    idat = bytearray()

    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        ctype = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, bit_depth, color_type, _comp, _filter, interlace = struct.unpack(">IIBBBBB", chunk
            )
        elif ctype == b"IDAT":
            idat.extend(chunk)
        elif ctype == b"IEND":
            break

    if width is None:
        raise RuntimeError("missing IHDR")
    if bit_depth != 8 or color_type != 6 or interlace != 0:
        raise RuntimeError("unsupported PNG format")

    raw = zlib.decompress(bytes(idat))
    stride = width * 4
    expected = height * (1 + stride)
    if len(raw) != expected:
        raise RuntimeError("unexpected raw size")

    out = bytearray(height * stride)
    src = 0
    prev = bytearray(stride)
    dst = 0

    def paeth(a: int, b: int, c: int) -> int:
        p = a + b - c
        pa = abs(p - a)
        pb = abs(p - b)
        pc = abs(p - c)
        if pa <= pb and pa <= pc:
            return a
        if pb <= pc:
            return b
        return c

    for _ in range(height):
        filt = raw[src]
        src += 1
        scan = bytearray(raw[src : src + stride])
        src += stride

        if filt == 1:
            for i in range(stride):
                left = scan[i - 4] if i >= 4 else 0
                scan[i] = (scan[i] + left) & 0xFF
        elif filt == 2:
            for i in range(stride):
                scan[i] = (scan[i] + prev[i]) & 0xFF
        elif filt == 3:
            for i in range(stride):
                left = scan[i - 4] if i >= 4 else 0
                scan[i] = (scan[i] + ((left + prev[i]) // 2)) & 0xFF
        elif filt == 4:
            for i in range(stride):
                left = scan[i - 4] if i >= 4 else 0
                up = prev[i]
                ul = prev[i - 4] if i >= 4 else 0
                scan[i] = (scan[i] + paeth(left, up, ul)) & 0xFF
        elif filt != 0:
            raise RuntimeError(f"unsupported filter {filt}")

        out[dst : dst + stride] = scan
        prev = scan
        dst += stride

    return width, height, bytes(out)


def analyze(width: int, height: int, buf: bytes) -> dict:
    min_x, min_y = width, height
    max_x, max_y = -1, -1
    text_px = 0
    bright = 0
    dark = 0

    # Background reference from corner.
    bgr = (buf[0], buf[1], buf[2])

    for y in range(height):
        row = y * width * 4
        for x in range(width):
            i = row + x * 4
            r, g, b = buf[i], buf[i + 1], buf[i + 2]
            dr = abs(r - bgr[0]) + abs(g - bgr[1]) + abs(b - bgr[2])
            if dr < 16:
                continue
            text_px += 1
            min_x = min(min_x, x)
            min_y = min(min_y, y)
            max_x = max(max_x, x)
            max_y = max(max_y, y)

            lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
            if lum >= 200:
                bright += 1
            elif lum <= 120:
                dark += 1

    if text_px == 0:
        raise AssertionError("no text-like pixels found")

    return {
        "text_px": text_px,
        "bbox_w": max_x - min_x + 1,
        "bbox_h": max_y - min_y + 1,
        "min_x": min_x,
        "min_y": min_y,
        "max_x": max_x,
        "max_y": max_y,
        "bright": bright,
        "dark": dark,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate and validate extrusion text PNG")
    parser.add_argument("--bin", default="./build/text_extrusion_png")
    parser.add_argument("--out", default="/tmp/motive_text_extrusion_test.png")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    bin_path = (repo_root / args.bin).resolve() if args.bin.startswith(".") else Path(args.bin).resolve()
    out_path = Path(args.out)

    if not bin_path.exists():
        raise FileNotFoundError(f"renderer binary not found: {bin_path}")

    subprocess.check_call(
        [
            str(bin_path),
            "--text",
            "What",
            "--out",
            str(out_path),
            "--pixel-height",
            "128",
            "--extrude-depth",
            "0.22",
            "--canvas-width",
            "1920",
            "--canvas-height",
            "1080",
        ],
        cwd=str(repo_root),
    )

    width, height, pix = decode_png_rgba8(out_path)
    stats = analyze(width, height, pix)

    if width < 1600 or height < 900:
        raise AssertionError(f"resolution too low: {width}x{height}")
    if stats["text_px"] < 30000:
        raise AssertionError(f"not enough rendered text coverage: {stats}")
    if stats["bbox_w"] < 420 or stats["bbox_h"] < 180:
        raise AssertionError(f"extruded text bbox too small: {stats}")
    if stats["bright"] < 10000:
        raise AssertionError(f"front face highlight missing: {stats}")
    if stats["dark"] < 5000:
        raise AssertionError(f"extrusion side/back shading missing: {stats}")

    print(f"Extrusion text PNG test passed: {out_path} :: {stats}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001
        print(f"FAILED: {exc}")
        sys.exit(1)
