# Telos tests

All automated test cases and test-only fixtures live below this directory.
Production components, including official Plugins, do not keep private test
trees. The top-level `tests/meson.build` is the single registration entry.

The suite is split by responsibility:

```text
tests/
├── unit/          focused Core and Linux platform tests
├── plugins/       Plugin ABI, lifecycle, installer, and implementation tests
│   └── fixtures/  shared modules and protocol data used only by tests
├── functional/    CLI, SDK/build contracts, and complete platform scenarios
├── acceptance.toml
├── check_acceptance.py
└── check_coverage.sh
```

Run everything:

```sh
meson test -C build --print-errorlogs
```

Run one category:

```sh
meson test -C build --suite unit --print-errorlogs
meson test -C build --suite plugins --print-errorlogs
meson test -C build --suite functional --print-errorlogs
```

Every Meson test must belong to exactly one of these suites. The acceptance
checker enforces that rule in addition to validating AC-1 through AC-16.
Keep globally visible test names stable because `acceptance.toml` maps those
names to product requirements.

The credentialed OpenAI smoke source also lives in `tests/plugins/`, but it is
a declared manual release gate rather than an automated Meson test. Enable it
with `-Dopenai_smoke=true`; automated suites never read remote credentials.

Plugin contributions put their focused test sources in `tests/plugins/`, not
inside the Plugin package. Functional scenarios may reuse fixtures from
`tests/plugins/fixtures/`, but a test belongs to the `functional` suite when
it exercises a complete user-visible workflow across multiple components.
