#!/usr/bin/env python3
"""Validate generated devicetree macros against macros.bnf grammar."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass
from typing import Final, NamedTuple

BNF_DEFAULT: Final[pathlib.Path] = pathlib.Path("doc/build/dts/macros.bnf")
RULE_DEFAULT: Final[str] = "dt-macro"


@dataclass(frozen=True)
class MacroDefinition:
    """Representation of a ``#define`` discovered in an input file."""

    path: pathlib.Path
    line: int
    name: str
    parameters: tuple[str, ...]


@dataclass(frozen=True)
class MacroViolation:
    """Information about a macro whose name does not match the grammar."""

    macro: MacroDefinition
    reason: str


class _Token(NamedTuple):
    kind: str
    value: object


class _Node:
    def to_regex(self, resolve: Callable[[str], str]) -> str:
        raise NotImplementedError


@dataclass
class _Literal(_Node):
    value: str

    def to_regex(self, resolve: Callable[[str], str]) -> str:  # noqa: D401
        return re.escape(self.value)


@dataclass
class _Range(_Node):
    start: int
    end: int

    def to_regex(self, resolve: Callable[[str], str]) -> str:
        start = re.escape(chr(self.start))
        end = re.escape(chr(self.end))
        if start == end:
            return start
        return f"[{start}-{end}]"


@dataclass
class _RuleRef(_Node):
    name: str

    def to_regex(self, resolve: Callable[[str], str]) -> str:
        return resolve(self.name)


@dataclass
class _Sequence(_Node):
    elements: list[_Node]

    def to_regex(self, resolve: Callable[[str], str]) -> str:
        return "".join(element.to_regex(resolve) for element in self.elements)


@dataclass
class _Alternation(_Node):
    options: list[_Node]

    def to_regex(self, resolve: Callable[[str], str]) -> str:
        rendered = [option.to_regex(resolve) for option in self.options]
        if not rendered:
            return ""
        if len(rendered) == 1:
            return rendered[0]
        return f"(?:{'|'.join(rendered)})"


@dataclass
class _Repetition(_Node):
    element: _Node
    minimum: int
    maximum: int | None

    def to_regex(self, resolve: Callable[[str], str]) -> str:
        body = self.element.to_regex(resolve)
        def grouped(text: str) -> str:
            if text.startswith("(?:") and text.endswith(")"):
                return text
            if len(text) == 1:
                return text
            return f"(?:{text})"

        body_grouped = grouped(body)
        if self.minimum == 0 and self.maximum == 1:
            return f"{body_grouped}?"
        if self.minimum == 0 and self.maximum is None:
            return f"{body_grouped}*"
        if self.minimum == 1 and self.maximum is None:
            return f"{body_grouped}+"
        if self.maximum is None:
            return f"{body_grouped}{{{self.minimum},}}"
        if self.minimum == self.maximum:
            return f"{body_grouped}{{{self.minimum}}}"
        return f"{body_grouped}{{{self.minimum},{self.maximum}}}"


class AbnfGrammar:
    """Minimal ABNF parser tailored for ``macros.bnf``."""

    _PREDEFINED: Final[dict[str, str]] = {
        "DIGIT": r"[0-9]",
    }

    def __init__(self, rules: dict[str, _Node]):
        self._rules = rules
        self._cache: dict[str, str] = {}

    @classmethod
    def from_path(cls, path: pathlib.Path) -> "AbnfGrammar":
        text = path.read_text(encoding="utf-8")
        rules = cls._parse_rules(text)
        return cls(rules)

    def compile(self, rule: str) -> re.Pattern[str]:
        regex = self._resolve(rule)
        return re.compile(f"^{regex}$", re.ASCII)

    def _resolve(self, name: str, stack: tuple[str, ...] = ()) -> str:
        if name in self._cache:
            return self._cache[name]
        if name in stack:
            raise ValueError(f"Recursive rule detected: {' -> '.join(stack + (name,))}")
        if name in self._PREDEFINED:
            self._cache[name] = self._PREDEFINED[name]
            return self._cache[name]
        if name not in self._rules:
            raise KeyError(f"Unknown rule '{name}' in macros grammar")
        regex = self._rules[name].to_regex(lambda ref: self._resolve(ref, stack + (name,)))
        self._cache[name] = regex
        return regex

    @staticmethod
    def _strip_comment(line: str) -> str:
        result = []
        in_quote = False
        for char in line:
            if char == '"':
                in_quote = not in_quote
            if char == ';' and not in_quote:
                break
            result.append(char)
        return "".join(result)

    @classmethod
    def _parse_rules(cls, text: str) -> dict[str, _Node]:
        raw_rules: dict[str, list[str]] = {}
        current: str | None = None
        for raw_line in text.splitlines():
            cleaned = cls._strip_comment(raw_line).strip()
            if not cleaned:
                continue
            match = re.match(r"^([A-Za-z0-9_-]+)\s*(=/|=)\s*(.*)$", cleaned)
            if match:
                name, operator, rhs = match.groups()
                if operator == "=" or name not in raw_rules:
                    raw_rules[name] = []
                raw_rules[name].append(rhs.strip())
                current = name
            else:
                if current is None:
                    continue
                raw_rules[current][-1] += " " + cleaned
        parsed: dict[str, _Node] = {}
        for name, expressions in raw_rules.items():
            alternatives: list[_Node] = []
            for expression in expressions:
                tokens = list(_tokenize(expression))
                parser = _ExpressionParser(tokens)
                node = parser.parse()
                if isinstance(node, _Alternation):
                    alternatives.extend(node.options)
                else:
                    alternatives.append(node)
            if len(alternatives) == 1:
                parsed[name] = alternatives[0]
            else:
                parsed[name] = _Alternation(alternatives)
        return parsed


class _ExpressionParser:
    def __init__(self, tokens: list[_Token]):
        self._tokens = tokens
        self._index = 0

    def parse(self) -> _Node:
        node = self._parse_expression()
        if self._index != len(self._tokens):
            raise ValueError("Unexpected tokens at end of expression")
        return node

    def _parse_expression(self) -> _Node:
        sequences = [self._parse_sequence()]
        while self._peek('/'):
            self._consume('/')
            sequences.append(self._parse_sequence())
        if len(sequences) == 1:
            return sequences[0]
        return _Alternation(sequences)

    def _parse_sequence(self) -> _Node:
        elements: list[_Node] = []
        while self._index < len(self._tokens) and self._tokens[self._index].kind not in {'/', ')', ']'}:
            elements.append(self._parse_element())
        return _Sequence(elements)

    def _parse_element(self) -> _Node:
        repeat = None
        if self._peek('repeat'):
            repeat = self._consume('repeat').value
        element = self._parse_factor()
        if repeat is None:
            return element
        minimum, maximum = repeat
        return _Repetition(element, minimum, maximum)

    def _parse_factor(self) -> _Node:
        token = self._tokens[self._index]
        if token.kind == '(':
            self._consume('(')
            node = self._parse_expression()
            self._consume(')')
            return node
        if token.kind == '[':
            self._consume('[')
            node = self._parse_expression()
            self._consume(']')
            return _Repetition(node, 0, 1)
        if token.kind == 'literal':
            self._consume('literal')
            return _Literal(token.value)
        if token.kind == 'range':
            self._consume('range')
            start, end = token.value
            return _Range(start, end)
        if token.kind == 'identifier':
            self._consume('identifier')
            return _RuleRef(token.value)
        raise ValueError(f"Unexpected token {token.kind!r}")

    def _peek(self, kind: str) -> bool:
        return self._index < len(self._tokens) and self._tokens[self._index].kind == kind

    def _consume(self, kind: str) -> _Token:
        if not self._peek(kind):
            raise ValueError(f"Expected token {kind!r}")
        token = self._tokens[self._index]
        self._index += 1
        return token


def _tokenize(expression: str) -> Iterator[_Token]:
    index = 0
    length = len(expression)
    while index < length:
        char = expression[index]
        if char.isspace():
            index += 1
            continue
        if expression.startswith('%s"', index):
            index += 3
            end = expression.index('"', index)
            literal = expression[index:end]
            index = end + 1
            yield _Token('literal', literal)
            continue
        if expression.startswith('%x', index):
            index += 2
            start_index = index
            while index < length and expression[index] not in ' []()/':
                index += 1
            value = expression[start_index:index]
            start_str, _, end_str = value.partition('-')
            start = int(start_str, 16)
            end = int(end_str, 16) if end_str else start
            yield _Token('range', (start, end))
            continue
        if char == '"':
            index += 1
            end = expression.index('"', index)
            literal = expression[index:end]
            index = end + 1
            yield _Token('literal', literal)
            continue
        if char in '()[]/':
            index += 1
            yield _Token(char, char)
            continue
        if char.isdigit() or char == '*':
            start_index = index
            while index < length and (expression[index].isdigit() or expression[index] == '*'):
                index += 1
            repeat = expression[start_index:index]
            if '*' in repeat:
                minimum, _, maximum = repeat.partition('*')
                min_value = int(minimum) if minimum else 0
                max_value = int(maximum) if maximum else None
                yield _Token('repeat', (min_value, max_value))
                continue
            yield _Token('literal', repeat)
            continue
        if char.isalpha() or char == '_':
            start_index = index
            while index < length and (expression[index].isalnum() or expression[index] in {'_', '-'}):
                index += 1
            identifier = expression[start_index:index]
            yield _Token('identifier', identifier)
            continue
        raise ValueError(f"Unsupported character '{char}' in expression '{expression}'")


def load_macros(paths: Iterable[pathlib.Path]) -> Iterator[MacroDefinition]:
    pattern = re.compile(r"^\s*#define\s+(DT_[A-Za-z0-9_]+)\s*(?:\(([^)]*)\))?", re.ASCII)
    for path in paths:
        with path.open(encoding="utf-8") as stream:
            for index, line in enumerate(stream, start=1):
                match = pattern.match(line)
                if not match:
                    continue
                name = match.group(1)
                params_raw = match.group(2)
                parameters: tuple[str, ...] = ()
                if params_raw is not None:
                    parameters = tuple(
                        part.strip()
                        for part in params_raw.split(',')
                        if part.strip()
                    )
                yield MacroDefinition(path=path, line=index, name=name, parameters=parameters)


def validate_macros(definitions: Iterable[MacroDefinition], pattern: re.Pattern[str]) -> list[MacroViolation]:
    violations: list[MacroViolation] = []
    for macro in definitions:
        if not pattern.fullmatch(macro.name):
            violations.append(
                MacroViolation(
                    macro=macro,
                    reason=f"macro name '{macro.name}' does not match rule",
                )
            )
    return violations


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths",
        nargs="+",
        type=pathlib.Path,
        help="Files containing generated devicetree macros",
    )
    parser.add_argument(
        "--bnf",
        type=pathlib.Path,
        default=BNF_DEFAULT,
        help=f"Path to macros.bnf grammar (default: {BNF_DEFAULT})",
    )
    parser.add_argument(
        "--rule",
        default=RULE_DEFAULT,
        help=f"Top-level grammar rule to validate against (default: {RULE_DEFAULT})",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(argv)
    grammar = AbnfGrammar.from_path(args.bnf)
    pattern = grammar.compile(args.rule)
    macros = list(load_macros(args.paths))
    violations = validate_macros(macros, pattern)
    if violations:
        for violation in violations:
            macro = violation.macro
            print(f"{macro.path}:{macro.line}: {violation.reason}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
