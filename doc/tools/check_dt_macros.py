#!/usr/bin/env python3
# Copyright (c) 2024 The Zephyr Project Developers
# SPDX-License-Identifier: Apache-2.0
"""Lightweight sanity checks for devicetree macro identifiers.

This script operates on a small, curated fixture that mimics the generated
``devicetree_generated.h`` header.  The goal is to exercise the grammar
documented in ``doc/build/dts/macros.bnf`` and make sure common patterns –
including identifiers for the devicetree root node – are accepted.

The fixture does not need to contain exhaustive data; it only needs to cover
typical shapes so regressions are caught quickly.  In particular, the root
node macros such as ``DT_N_PATH`` and ``DT_N_FOREACH_CHILD(fn)`` are included
so the optional nature of the ``path-id`` production is validated.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator

FIXTURE_DIR = Path(__file__).with_name("fixtures")
FIXTURE = FIXTURE_DIR / "sample_devicetree_macros.h"

PATH_SEGMENT_RE = re.compile(r"_S_([a-z0-9]+(?:_[a-z0-9]+)*)")


@dataclass(frozen=True)
class Macro:
    name: str
    params: str


MACRO_RE = re.compile(r"#define\s+(DT_[A-Za-z0-9_]+)(\([^)]*\))?")


def parse_fixture(path: Path) -> Iterator[Macro]:
    """Yield ``Macro`` objects from a fixture header."""

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        match = MACRO_RE.match(line)
        if match is None:
            continue

        macro_name, params = match.groups()
        yield Macro(name=macro_name, params=params or "")


def consume_path_id(text: str) -> int:
    """Return the index directly after the ``path-id`` portion of ``text``."""

    index = 0
    while True:
        match = PATH_SEGMENT_RE.match(text, index)
        if match is None:
            break
        index = match.end()

    return index


def validate_node_macro(macro: Macro) -> None:
    """Validate macros that start with the ``DT_N`` prefix."""

    suffix_params = {
        "_PATH": "",
        "_FOREACH_CHILD": "(fn)",
        "_FOREACH_CHILD_SEP": "(fn, sep)",
        "_FOREACH_CHILD_VARGS": "(fn, ...)",
        "_FOREACH_CHILD_SEP_VARGS": "(fn, sep, ...)",
    }

    name_without_prefix = macro.name[len("DT_") :]
    if not name_without_prefix.startswith("N"):
        return

    tail = name_without_prefix[1:]
    try:
        path_id_end = consume_path_id(tail)
    except ValueError as exc:
        raise AssertionError(f"invalid path-id in '{macro.name}'") from exc

    suffix = tail[path_id_end:]
    for candidate, expected_params in suffix_params.items():
        if suffix == candidate:
            if macro.params != expected_params:
                raise AssertionError(
                    f"macro '{macro.name}' expects parameters {expected_params!r},"
                    f" found {macro.params!r}"
                )
            return

    raise AssertionError(f"unrecognised node macro suffix in '{macro.name}'")


def validate_macros(macros: Iterable[Macro]) -> None:
    for macro in macros:
        if macro.name.startswith("DT_N"):
            validate_node_macro(macro)


def main() -> int:
    if not FIXTURE.exists():
        print(f"Fixture '{FIXTURE}' not found", file=sys.stderr)
        return 1

    macros = list(parse_fixture(FIXTURE))
    try:
        validate_macros(macros)
    except AssertionError as exc:
        print(exc, file=sys.stderr)
        return 1

    print(f"Validated {len(macros)} macros from {FIXTURE.relative_to(Path.cwd())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
