#!/usr/bin/env python3

"""Check the Telos top-level C function declaration layout."""

from __future__ import annotations

import pathlib
import re
import sys


FUNCTION_SIGNATURE = re.compile(
    r"^\s*(?!#)(?!typedef\b)(?![A-Z][A-Z0-9_]*\s*\()(?!=)"
    r"[^=;]*?(?P<name>[a-z_][_A-Za-z0-9]*)\("
)
FUNCTION_POINTER_SIGNATURE = re.compile(
    r"^\s*(?:typedef\b.*?)?\(\*(?P<name>[a-z_][_A-Za-z0-9]*)\)\("
)


def sanitize(line: str, in_comment: bool) -> tuple[str, bool]:
    """Remove comments and literals while preserving structural characters."""

    output: list[str] = []
    index = 0
    quote = ""
    while index < len(line):
        current = line[index]
        following = line[index + 1] if index + 1 < len(line) else ""
        if in_comment:
            if current == "*" and following == "/":
                in_comment = False
                output.extend("  ")
                index += 2
            else:
                output.append(" ")
                index += 1
            continue
        if quote:
            output.append(" ")
            if current == "\\" and following:
                output.append(" ")
                index += 2
                continue
            if current == quote:
                quote = ""
            index += 1
            continue
        if current == "/" and following == "*":
            in_comment = True
            output.extend("  ")
            index += 2
        elif current == "/" and following == "/":
            output.extend(" " * (len(line) - index))
            break
        elif current in {'"', "'"}:
            quote = current
            output.append(" ")
            index += 1
        else:
            output.append(current)
            index += 1
    return "".join(output), in_comment


def check(path: pathlib.Path) -> list[str]:
    failures: list[str] = []
    brace_depth = 0
    signature_depth = 0
    signature_column = 0
    expect_parameter = False
    in_comment = False

    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        return [f"{path}: could not read source: {error}"]

    for number, raw in enumerate(lines, 1):
        clean, in_comment = sanitize(raw, in_comment)
        stripped = clean.strip()

        if brace_depth == 0 and signature_depth == 0:
            match = FUNCTION_SIGNATURE.match(clean)
            pointer_match = FUNCTION_POINTER_SIGNATURE.match(clean)
            opening = -1
            if match is not None:
                if match.group("name") == "_Static_assert":
                    brace_depth += clean.count("{") - clean.count("}")
                    continue
                opening = clean.find("(", match.start("name"))
            elif pointer_match is not None:
                opening = pointer_match.end() - 1
            if opening >= 0:
                signature_column = opening + 1
                signature_depth = clean[opening:].count("(") - clean[
                    opening:
                ].count(")")
                if clean[opening + 1 :].strip() == "":
                    failures.append(
                        f"{path}:{number}: first parameter must follow the "
                        "opening parenthesis"
                    )
                expect_parameter = signature_depth > 0 and clean.rstrip().endswith(
                    ","
                )
                if signature_depth == 0:
                    signature_column = 0
                    expect_parameter = False
        elif signature_depth > 0:
            if expect_parameter and stripped and stripped not in {")", ");"}:
                leading = raw[: len(raw) - len(raw.lstrip())]
                if "\t" in leading or len(leading) != signature_column:
                    failures.append(
                        f"{path}:{number}: parameter must align to column "
                        f"{signature_column + 1}"
                    )
            if stripped in {")", ");"}:
                failures.append(
                    f"{path}:{number}: closing parenthesis must follow the "
                    "last parameter"
                )
            signature_depth += clean.count("(") - clean.count(")")
            expect_parameter = signature_depth > 0 and clean.rstrip().endswith(
                ","
            )
            if signature_depth <= 0:
                signature_depth = 0
                signature_column = 0
                expect_parameter = False

        brace_depth += clean.count("{") - clean.count("}")
        if brace_depth < 0:
            brace_depth = 0
        if brace_depth > 0:
            signature_depth = 0

    return failures


def check_macros(path: pathlib.Path) -> list[str]:
    failures: list[str] = []
    continuation_lines: list[tuple[int, str]] = []

    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        return [f"{path}: could not read source: {error}"]

    def finish_macro() -> None:
        if not continuation_lines:
            return
        columns = [line.rfind("\\") for _, line in continuation_lines]
        if len(set(columns)) != 1:
            number = continuation_lines[0][0]
            failures.append(
                f"{path}:{number}: macro continuation backslashes must align"
            )
        for number, line in continuation_lines:
            column = line.rfind("\\")
            content = line[:column]
            gap_start = len(content.rstrip(" \t"))
            gap = content[gap_start:]
            if not gap or any(character != " " for character in gap):
                failures.append(
                    f"{path}:{number}: use spaces before the macro "
                    "continuation backslash"
                )
        continuation_lines.clear()

    in_macro = False
    for number, line in enumerate(lines, 1):
        stripped = line.rstrip()
        starts_macro = re.match(r"^\s*#\s*define\b", line) is not None
        if not in_macro and starts_macro and stripped.endswith("\\"):
            in_macro = True
        if in_macro:
            if stripped.endswith("\\"):
                continuation_lines.append((number, line))
            else:
                finish_macro()
                in_macro = False
    finish_macro()
    return failures


def main(arguments: list[str]) -> int:
    failures: list[str] = []

    if not arguments:
        print("usage: check-function-layout.py FILE...", file=sys.stderr)
        return 2
    for argument in arguments:
        path = pathlib.Path(argument)
        failures.extend(check(path))
        failures.extend(check_macros(path))
    for failure in failures:
        print(failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
