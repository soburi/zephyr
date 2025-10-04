"""Tests for doc.tools.check_dt_macros."""

from __future__ import annotations

import importlib.util
import pathlib
import sys

import pytest


@pytest.fixture(scope="module")
def checker_module():
    module_path = pathlib.Path(__file__).resolve().parents[3] / "doc" / "tools" / "check_dt_macros.py"
    spec = importlib.util.spec_from_file_location("doc.tools.check_dt_macros", module_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)  # type: ignore[attr-defined]
    return module


@pytest.fixture(scope="module")
def grammar(checker_module):
    grammar_path = pathlib.Path(__file__).resolve().parents[3] / "doc" / "build" / "dts" / "macros.bnf"
    return checker_module.AbnfGrammar.from_path(grammar_path)


def test_fixture_macros_match_abnf(checker_module, grammar):
    fixture = pathlib.Path(__file__).resolve().parent / "fixtures" / "devicetree_generated_sample.h"
    pattern = grammar.compile("dt-macro")
    macros = list(checker_module.load_macros([fixture]))
    violations = checker_module.validate_macros(macros, pattern)
    assert not violations, "All macros in the fixture should match the ABNF grammar"


def test_invalid_macro_is_reported(tmp_path, checker_module, grammar):
    invalid = tmp_path / "invalid.h"
    invalid.write_text("#define DT_invalid_macro 1\n", encoding="utf-8")
    pattern = grammar.compile("dt-macro")
    macros = list(checker_module.load_macros([invalid]))
    violations = checker_module.validate_macros(macros, pattern)
    assert len(violations) == 1
    assert violations[0].macro.line == 1
    assert "DT_invalid_macro" in violations[0].reason


def test_macro_parameters_are_tokenized(checker_module):
    fixture = pathlib.Path(__file__).resolve().parent / "fixtures" / "devicetree_generated_sample.h"
    macros = list(checker_module.load_macros([fixture]))
    macro = next(m for m in macros if m.name.endswith("FOREACH_CHILD_SEP_VARGS"))
    assert macro.parameters == ("fn", "sep", "...")
