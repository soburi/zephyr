#!/usr/bin/env python3
"""Validate RFC 7405 ABNF grammars used in Zephyr documentation.

This script implements a small ABNF parser that can be used to sanity check
grammar fragments such as ``doc/build/dts/macros.bnf``.  It focuses on the
constructs that appear in the Zephyr documentation today (rule concatenation,
repetition, groups, options, literals and numeric values) and reports the first
syntax error it encounters with a helpful line/column location.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import pathlib
import re
import sys
from typing import Iterable, List, Optional, Sequence, Tuple


class ABNFError(RuntimeError):
    """Raised when a syntax error is encountered in the grammar."""

    def __init__(self, message: str, *, line: int, column: int, rule: Optional[str]):
        super().__init__(message)
        self.line = line
        self.column = column
        self.rule = rule

    def __str__(self) -> str:  # pragma: no cover - human readable error
        location = f"line {self.line}, column {self.column}"
        if self.rule:
            location += f" (in rule '{self.rule}')"
        return f"{location}: {super().__str__()}"


@dataclass
class Token:
    kind: str
    value: str
    column: int


_TOKEN_REGEX = re.compile(
    r"(?P<WS>\s+)"  # whitespace
    r"|(?P<DEFINED_AS>=/)"
    r"|(?P<EQUAL>=)"
    r"|(?P<LPAREN>\()"
    r"|(?P<RPAREN>\))"
    r"|(?P<LBRK>\[)"
    r"|(?P<RBRK>\])"
    r"|(?P<SLASH>/)"
    r"|(?P<STAR>\*)"
    r'|(?P<CASE_STRING>%[si]"(?:\\.|[^"\\])*")'
    r"|(?P<NUM_VAL>%[bdx](?:[0-9A-F]+(?:-[0-9A-F]+)?)(?:\.[0-9A-F]+(?:-[0-9A-F]+)?)*)"
    r'|(?P<CHAR_VAL>"(?:\\.|[^"\\])*")'
    r"|(?P<PROSE_VAL><(?:\\.|[^>\\])*>)"
    r"|(?P<RULENAME>[A-Za-z][A-Za-z0-9-]*)"
    r"|(?P<NUMBER>\d+)"
    ,
)


class Lexer:
    def __init__(self, text: str, *, line_offset: int) -> None:
        self.text = text
        self.position = 0
        self.line_offset = line_offset

    def __iter__(self) -> Iterable[Token]:
        while self.position < len(self.text):
            match = _TOKEN_REGEX.match(self.text, self.position)
            if not match:
                column = self.position + 1
                raise ABNFError(
                    f"unexpected character {self.text[self.position]!r}",
                    line=self.line_offset,
                    column=column,
                    rule=None,
                )
            if match.lastgroup is None:  # whitespace
                self.position = match.end()
                continue
            token = Token(match.lastgroup, match.group(match.lastgroup), match.start() + 1)
            self.position = match.end()
            yield token
        yield Token("EOF", "", len(self.text) + 1)


class Parser:
    def __init__(self, tokens: Sequence[Token], *, rule_name: str, line: int) -> None:
        self.tokens = list(tokens)
        self.index = 0
        self.rule_name = rule_name
        self.line = line

    def current(self) -> Token:
        return self.tokens[self.index]

    def consume(self, kind: str) -> Token:
        token = self.current()
        if token.kind != kind:
            raise ABNFError(
                f"expected {kind} but found {token.kind}",
                line=self.line,
                column=token.column,
                rule=self.rule_name,
            )
        self.index += 1
        return token

    def match(self, kind: str) -> Optional[Token]:
        token = self.current()
        if token.kind == kind:
            self.index += 1
            return token
        return None

    def parse(self) -> None:
        self._parse_rule()
        if self.current().kind != "EOF":
            token = self.current()
            raise ABNFError(
                "unexpected trailing input",
                line=self.line,
                column=token.column,
                rule=self.rule_name,
            )

    def _parse_rule(self) -> None:
        self.consume("RULENAME")
        self._consume_whitespace()
        if self.match("DEFINED_AS") is None:
            self.consume("EQUAL")
        self._consume_whitespace()
        self._parse_elements()

    def _parse_elements(self) -> None:
        self._parse_alternation()

    def _parse_alternation(self) -> None:
        self._parse_concatenation()
        while True:
            self._consume_whitespace()
            checkpoint = self.index
            if self.match("SLASH") is None:
                self.index = checkpoint
                break
            self._consume_whitespace()
            self._parse_concatenation()

    def _parse_concatenation(self) -> None:
        self._parse_repetition()
        while True:
            checkpoint = self.index
            if not self._consume_whitespace():
                self.index = checkpoint
                break
            token = self.current()
            if token.kind in {"SLASH", "RBRK", "RPAREN", "EOF"}:
                self.index = checkpoint
                break
            self._parse_repetition()

    def _consume_whitespace(self) -> bool:
        consumed = False
        while self.current().kind == "WS":
            consumed = True
            self.index += 1
        return consumed

    def _parse_repetition(self) -> None:
        self._maybe_parse_repeat()
        self._consume_whitespace()
        self._parse_element()

    def _maybe_parse_repeat(self) -> None:
        token = self.current()
        if token.kind == "NUMBER":
            self.index += 1
            if self.match("STAR") is not None:
                if self.current().kind == "NUMBER":
                    self.index += 1
            else:
                return
        elif token.kind == "STAR":
            self.index += 1
            if self.current().kind == "NUMBER":
                self.index += 1

    def _parse_element(self) -> None:
        token = self.current()
        if token.kind in {"RULENAME", "CHAR_VAL", "CASE_STRING", "NUM_VAL", "PROSE_VAL"}:
            self.index += 1
            return
        if token.kind == "LBRK":
            self.index += 1
            self._consume_whitespace()
            self._parse_alternation()
            self._consume_whitespace()
            self.consume("RBRK")
            return
        if token.kind == "LPAREN":
            self.index += 1
            self._consume_whitespace()
            self._parse_alternation()
            self._consume_whitespace()
            self.consume("RPAREN")
            return
        raise ABNFError(
            f"unexpected token {token.kind}",
            line=self.line,
            column=token.column,
            rule=self.rule_name,
        )


def strip_comments(lines: Iterable[str]) -> List[Tuple[str, int]]:
    cleaned: List[Tuple[str, int]] = []
    for line_number, raw_line in enumerate(lines, start=1):
        in_string = False
        escape = False
        result_chars: List[str] = []
        for ch in raw_line.rstrip("\n"):
            if ch == "\\" and not escape:
                escape = True
                result_chars.append(ch)
                continue
            if ch == '"' and not escape:
                in_string = not in_string
            if ch == ';' and not in_string:
                break
            result_chars.append(ch)
            escape = False
        cleaned_line = "".join(result_chars).rstrip()
        if cleaned_line:
            cleaned.append((cleaned_line, line_number))
    return cleaned


def join_rule_lines(cleaned_lines: Sequence[Tuple[str, int]]) -> List[Tuple[str, int]]:
    rules: List[Tuple[str, int]] = []
    current_line = ""
    start_number = 0
    for text, line_number in cleaned_lines:
        if text and not text[0].isspace():
            if current_line:
                rules.append((current_line, start_number))
            current_line = text.strip()
            start_number = line_number
        else:
            if not current_line:
                raise ABNFError(
                    "continuation line encountered without preceding rule",
                    line=line_number,
                    column=1,
                    rule=None,
                )
            current_line += " " + text.strip()
    if current_line:
        rules.append((current_line, start_number))
    return rules


def validate_rule(rule_text: str, line_number: int) -> None:
    # Tokenise the rule.
    tokens: List[Token] = []
    lexer = Lexer(rule_text, line_offset=line_number)
    for token in lexer:
        if token.kind == "EOF":
            tokens.append(Token("EOF", "", token.column))
        elif token.kind == "WS":
            tokens.append(Token("WS", token.value, token.column))
        else:
            tokens.append(token)
    if not tokens:
        return
    rule_name = tokens[0].value if tokens and tokens[0].kind == "RULENAME" else None
    parser = Parser(tokens, rule_name=rule_name or "<unknown>", line=line_number)
    parser.parse()


def validate_file(path: pathlib.Path) -> None:
    with path.open("r", encoding="utf-8") as fp:
        cleaned = strip_comments(fp)
    rules = join_rule_lines(cleaned)
    for rule_text, line_number in rules:
        validate_rule(rule_text, line_number)


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Validate RFC 7405 ABNF files.")
    parser.add_argument("paths", metavar="FILE", nargs="+", type=pathlib.Path)
    args = parser.parse_args(argv)

    had_error = False
    for path in args.paths:
        try:
            validate_file(path)
        except ABNFError as exc:  # pragma: no cover - CLI entry point
            had_error = True
            print(f"{path}: {exc}", file=sys.stderr)
    return 1 if had_error else 0


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    sys.exit(main())
