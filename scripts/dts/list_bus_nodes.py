#!/usr/bin/env python3
"""Utility for listing bindings that define or use buses."""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path
from typing import Iterable, List, Sequence, Tuple


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
PYTHON_DEVICETREE = SCRIPT_DIR / "python-devicetree" / "src"

# Ensure the in-tree python-devicetree sources are preferred.
sys.path.insert(0, str(PYTHON_DEVICETREE))

try:
    from devicetree.edtlib import Binding, bindings_from_paths  # type: ignore  # noqa: E402
except ModuleNotFoundError as exc:  # pragma: no cover - environment guard
    if exc.name == "yaml":
        raise SystemExit(
            "PyYAML is required to load bindings. Install it with 'pip install PyYAML'."
        ) from exc
    raise


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "List bindings that declare a bus or appear on a bus, including "
            "their child-binding definitions."
        )
    )
    default_root = REPO_ROOT / "dts" / "bindings"
    parser.add_argument(
        "--bindings-root",
        type=Path,
        default=default_root,
        help=(
            "Root directory to search for binding YAML files (default: %(default)s)."
        ),
    )
    parser.add_argument(
        "--csv",
        action="store_true",
        help="Emit the table as comma-separated values instead of padded text.",
    )
    return parser.parse_args()


def find_binding_files(root: Path) -> List[Path]:
    if not root.is_dir():
        raise SystemExit(f"Bindings root '{root}' does not exist or is not a directory")

    files = {
        path.resolve()
        for suffix in ("*.yaml", "*.yml")
        for path in root.rglob(suffix)
        if path.is_file()
    }
    if not files:
        raise SystemExit(f"No binding YAML files found under '{root}'")
    return sorted(files)


def relative_label(path: Path, root: Path, depth: int) -> str:
    try:
        rel = path.resolve().relative_to(root.resolve())
    except ValueError:
        rel = path.name

    if depth == 0:
        return str(rel)
    if depth == 1:
        suffix = "child-binding"
    else:
        suffix = f"child-binding depth {depth}"
    return f"{rel} ({suffix})"


def collect_binding_rows(binding: Binding, root: Path, depth: int = 0) -> List[Tuple[str, str, str]]:
    rows: List[Tuple[str, str, str]] = []
    binding_path = Path(binding.path) if binding.path else None

    if binding_path is not None and (binding.buses or binding.on_bus):
        label = relative_label(binding_path, root, depth)
        buses = ", ".join(binding.buses) if binding.buses else ""
        on_bus = binding.on_bus or ""
        rows.append((label, buses, on_bus))

    if binding.child_binding is not None:
        rows.extend(collect_binding_rows(binding.child_binding, root, depth + 1))

    return rows


def sort_rows(rows: Iterable[Tuple[str, str, str]]) -> List[Tuple[str, str, str]]:
    return sorted(rows, key=lambda row: row[0])


def emit_csv(rows: Sequence[Tuple[str, str, str]]) -> None:
    writer = csv.writer(sys.stdout)
    writer.writerow(["Binding", "Buses", "On Bus"])
    for row in rows:
        writer.writerow(row)


def emit_table(rows: Sequence[Tuple[str, str, str]]) -> None:
    headers = ("Binding", "Buses", "On Bus")
    if not rows:
        print("No bus-related bindings found.")
        return

    widths = [len(header) for header in headers]
    for row in rows:
        for idx, value in enumerate(row):
            widths[idx] = max(widths[idx], len(value))

    header_line = "  ".join(header.ljust(widths[idx]) for idx, header in enumerate(headers))
    separator = "  ".join("-" * widths[idx] for idx in range(len(headers)))
    print(header_line)
    print(separator)
    for row in rows:
        print("  ".join(value.ljust(widths[idx]) for idx, value in enumerate(row)))


def main() -> None:
    args = parse_args()
    bindings_root = args.bindings_root.resolve()
    binding_files = [str(path) for path in find_binding_files(bindings_root)]

    bindings = bindings_from_paths(binding_files)

    rows: List[Tuple[str, str, str]] = []
    for binding in bindings:
        rows.extend(collect_binding_rows(binding, bindings_root))

    rows = sort_rows(rows)

    if args.csv:
        emit_csv(rows)
    else:
        emit_table(rows)


if __name__ == "__main__":
    main()
