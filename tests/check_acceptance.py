#!/usr/bin/env python3

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tomllib


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate Telos acceptance-to-test traceability.",
    )
    parser.add_argument("manifest", type=Path)
    parser.add_argument("build_directory", type=Path)
    parser.add_argument("--test-log", type=Path)
    return parser.parse_args()


def configured_tests(build_directory: Path) -> dict[str, set[str]]:
    completed = subprocess.run(
        ["meson", "introspect", str(build_directory), "--tests"],
        check=True,
        capture_output=True,
        text=True,
    )
    return {
        entry["name"]: {
            suite.rsplit(":", 1)[-1] for suite in entry.get("suite", [])
        }
        for entry in json.loads(completed.stdout)
    }


def check_test_log(path: Path, approved_skips: set[str]) -> list[str]:
    errors = []
    with path.open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            record = json.loads(line)
            name = record["name"].split(":", 1)[-1]
            if record["result"] == "SKIP" and name not in approved_skips:
                errors.append(
                    f"{path}:{line_number}: unapproved test skip: {name}"
                )
            elif record["result"] not in {"OK", "SKIP"}:
                errors.append(
                    f"{path}:{line_number}: test did not pass: "
                    f"{name} ({record['result']})"
                )
    return errors


def main() -> int:
    arguments = parse_arguments()
    with arguments.manifest.open("rb") as stream:
        manifest = tomllib.load(stream)

    expected_criteria = {f"AC-{number}" for number in range(1, 17)}
    criteria = manifest.get("criteria", {})
    errors = []
    if set(criteria) != expected_criteria:
        missing = sorted(expected_criteria - set(criteria))
        extra = sorted(set(criteria) - expected_criteria)
        errors.append(f"criterion set mismatch: missing={missing} extra={extra}")

    tests = configured_tests(arguments.build_directory)
    required_suites = {"unit", "plugins", "functional"}
    observed_suites = set()
    for name, suites in sorted(tests.items()):
        categories = suites & required_suites
        observed_suites.update(categories)
        if len(categories) != 1:
            errors.append(
                f"{name} must belong to exactly one test suite: "
                f"{sorted(required_suites)}"
            )
    missing_suites = sorted(required_suites - observed_suites)
    if missing_suites:
        errors.append(f"configured test suites are missing: {missing_suites}")

    verification = manifest.get("verification", {})
    conditional_tests = set(verification.get("conditional_tests", []))

    for criterion in sorted(criteria):
        mapped_tests = criteria[criterion].get("tests", [])
        external = criteria[criterion].get("external", [])
        if not mapped_tests:
            errors.append(f"{criterion} has no automated test mapping")
        if not mapped_tests and not external:
            errors.append(f"{criterion} has no verification mapping")
        missing_tests = sorted(
            set(mapped_tests) - set(tests) - conditional_tests
        )
        if missing_tests:
            errors.append(
                f"{criterion} names unconfigured tests: {missing_tests}"
            )

    manual = verification.get("manual", [])
    if manual != ["credentialed-openai-responses-smoke"]:
        errors.append("manual remote-provider smoke mapping is missing")
    mapped_names = {
        name
        for entry in criteria.values()
        for name in entry.get("tests", [])
    }
    unknown_conditional = sorted(conditional_tests - mapped_names)
    if unknown_conditional:
        errors.append(
            "verification names unknown conditional tests: "
            f"{unknown_conditional}"
        )

    if arguments.test_log is not None:
        approved = set(verification.get("approved_skips", []))
        errors.extend(check_test_log(arguments.test_log, approved))

    if errors:
        for error in errors:
            print(f"acceptance: {error}", file=sys.stderr)
        return 1

    mapped_count = sum(
        len(entry.get("tests", [])) for entry in criteria.values()
    )
    print(
        f"Acceptance traceability passed: {len(criteria)} criteria, "
        f"{mapped_count} test mappings, {len(tests)} tests in "
        f"{len(required_suites)} suites, no unapproved skips."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
