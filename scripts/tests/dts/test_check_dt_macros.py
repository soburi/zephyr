"""Tests for the devicetree macro grammar checker."""

from importlib import util
import sys
from pathlib import Path
from types import ModuleType

import pytest


FIXTURE_DIR = Path(__file__).parent / "fixtures"
REPO_ROOT = Path(__file__).resolve().parents[3]
GRAMMAR_FILE = REPO_ROOT / "doc" / "build" / "dts" / "macros.bnf"
CHECKER_PATH = REPO_ROOT / "doc" / "tools" / "check_dt_macros.py"


def _load_checker() -> ModuleType:
    spec = util.spec_from_file_location("check_dt_macros", CHECKER_PATH)
    if spec is None or spec.loader is None:  # pragma: no cover - defensive
        raise RuntimeError("Unable to load check_dt_macros module")
    module = util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


@pytest.mark.parametrize(
    "header", [FIXTURE_DIR / "devicetree_generated_sample.h"],
)
def test_generated_macros_match_grammar(header: Path) -> None:
    """Ensure that the sample header passes the grammar validation."""

    checker = _load_checker()
    invalid = checker.validate_macros(
        header.read_text(encoding="utf-8"),
        GRAMMAR_FILE.read_text(encoding="utf-8"),
    )

    assert invalid == []
