#!/usr/bin/env python3
"""Warn/fail when C++ source files exceed a line threshold.

Google C++ Style Guide emphasizes readability and manageable units.
This helper enforces a practical max file length for translation units.
"""

from __future__ import annotations

import argparse
import pathlib
import sys


def count_lines(path: pathlib.Path) -> int:
    with path.open("r", encoding="utf-8", errors="ignore") as f:
        return sum(1 for _ in f)


def main() -> int:
    parser = argparse.ArgumentParser(description="Check C++ file line counts")
    parser.add_argument("--root", default=".", help="Repo root")
    parser.add_argument("--max-lines", type=int, default=1500, help="Maximum allowed lines per file")
    parser.add_argument("--fail", action="store_true", help="Exit non-zero on violations")
    parser.add_argument(
        "--exclude-dir",
        action="append",
        default=[],
        help="Directory prefix (relative to root) to exclude; can be repeated",
    )
    parser.add_argument(
        "--only",
        action="append",
        default=[],
        help="File path (relative to root) to include exclusively; can be repeated",
    )
    args = parser.parse_args()

    root = pathlib.Path(args.root).resolve()
    excluded_dirs = {p.strip("/").replace("\\", "/") for p in args.exclude_dir if p.strip()}
    only_paths = {p.strip("/").replace("\\", "/") for p in args.only if p.strip()}

    offenders: list[tuple[pathlib.Path, int]] = []
    for path in sorted(root.rglob("*.cpp")):
        rel = path.relative_to(root).as_posix()
        if rel.startswith("build/"):
            continue
        if only_paths and rel not in only_paths:
            continue
        if any(rel == prefix or rel.startswith(prefix + "/") for prefix in excluded_dirs):
            continue
        line_count = count_lines(path)
        if line_count > args.max_lines:
            offenders.append((path, line_count))

    if not offenders:
        print(f"OK: no .cpp files exceed {args.max_lines} lines")
        return 0

    print(f"Found {len(offenders)} .cpp files over {args.max_lines} lines:")
    for path, lines in offenders:
        print(f"  {lines:5d}  {path.relative_to(root)}")

    return 1 if args.fail else 0


if __name__ == "__main__":
    sys.exit(main())
