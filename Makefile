# Telos GNU Make convenience wrapper.
#
# This Makefile wraps the Meson/Ninja commands used for a Linux host build so
# common workflows do not require remembering the underlying tool chain. It is
# a thin convenience layer: the Meson build directory remains the source of
# truth, and each target simply assembles the well-known command.
#
# Common usage:
#
#   make                 build the Linux host (default target)
#   make build           same as the default target
#   make run             start the interactive terminal Agent
#   make run PROMPT=hi   run one non-interactive Agent turn
#   make chat PROMPT=hi  alias of `run` for a single prompt
#   make test            run every unit, Plugin, and functional test
#   make test-unit       run only the unit suite
#   make test-plugins    run only the Plugin suite
#   make test-functional run only the functional suite
#   make doctor          print a host health summary
#   make check           run the QEMU-derived style checks on all sources
#   make acceptance      verify acceptance traceability against the build
#   make sanitize        build and test with AddressSanitizer and UBSan
#   make coverage        run the coverage gate
#   make clean           remove the configured build directory
#   make distclean       remove every generated build directory
#
# Overridable variables:
#
#   BUILD_DIR   Meson build directory (default: build)
#   MESON       Meson executable    (default: meson)
#   CC           C compiler passed to Meson setup
#   CONFIGURE_OPTS extra arguments appended to `meson setup`
#   PROMPT       prompt for the `run`/`chat` one-shot targets
#   TESTS        extra arguments passed to `meson test`
#   TELOS_ENV    extra environment variables prefix for the run targets
#
# Zephyr and QEMU targets are intentionally omitted here. They require a west
# workspace separate from this repository; see docs/testing.md for the
# workspace layout and run_zephyr_*.sh runners.

MAKEFLAGS += --no-print-directory

# Bare `make` must build the host, not the configure helper that happens to
# appear first in this file.
.DEFAULT_GOAL := build

BUILD_DIR ?= build
MESON ?= meson
CONFIGURE_OPTS ?= -Dcurl_transport=enabled
CC ?= cc
TESTS ?=
PROMPT ?=
TELOS_ENV ?=

TELOS_BIN := $(BUILD_DIR)/tools/telos
CONFIGURED := $(shell test -f $(BUILD_DIR)/meson-private/coredata.dat \
	&& echo yes || echo no)

.PHONY: configure
configure:
	@if test "$(CONFIGURED)" = yes; then \
		echo "  [$(BUILD_DIR)] already configured"; \
	else \
		echo "  [meson] setup $(BUILD_DIR)"; \
		CC="$(CC)" $(MESON) setup $(BUILD_DIR) $(CONFIGURE_OPTS); \
	fi

.PHONY: build
build: configure
	@echo "  [ninja] compile $(BUILD_DIR)"
	$(MESON) compile -C $(BUILD_DIR)

.PHONY: reconfigure
reconfigure:
	@echo "  [meson] reconfigure $(BUILD_DIR)"
	$(MESON) setup --reconfigure $(BUILD_DIR) $(CONFIGURE_OPTS)

.PHONY: test
test: build
	@echo "  [meson] test $(BUILD_DIR)"
	$(MESON) test -C $(BUILD_DIR) --print-errorlogs $(TESTS)

.PHONY: test-unit
test-unit: build
	$(MESON) test -C $(BUILD_DIR) --suite unit --print-errorlogs $(TESTS)

.PHONY: test-plugins
test-plugins: build
	$(MESON) test -C $(BUILD_DIR) --suite plugins --print-errorlogs $(TESTS)

.PHONY: test-functional
test-functional: build
	$(MESON) test -C $(BUILD_DIR) --suite functional --print-errorlogs $(TESTS)

.PHONY: run chat
run chat: build
	@if test -n "$(PROMPT)"; then \
		echo "  [run] $(TELOS_BIN) run '$(PROMPT)'"; \
		$(TELOS_ENV) $(TELOS_BIN) run "$(PROMPT)"; \
	else \
		echo "  [run] $(TELOS_BIN) (interactive)"; \
		$(TELOS_ENV) $(TELOS_BIN); \
	fi

.PHONY: doctor
doctor: build
	$(TELOS_BIN) doctor

.PHONY: check style
check style:
	@echo "  [style] check function layout and checkpatch rules"
	scripts/check-style.sh --all

.PHONY: acceptance
acceptance: build
	@echo "  [acceptance] verify traceability against $(BUILD_DIR)"
	python3 tests/check_acceptance.py \
		tests/acceptance.toml \
		$(BUILD_DIR)

.PHONY: sanitize
sanitize:
	@echo "  [sanitize] AddressSanitizer + UndefinedBehaviorSanitizer"
	CC=clang meson setup --wipe build-sanitize \
		-Db_sanitize=address,undefined \
		-Db_lundef=false
	$(MESON) compile -C build-sanitize
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		$(MESON) test -C build-sanitize --print-errorlogs

.PHONY: coverage
coverage:
	@echo "  [coverage] gcovr line >= 90 and branch >= 80"
	tests/check_coverage.sh build-coverage

.PHONY: clean
clean:
	@echo "  [clean] remove $(BUILD_DIR)"
	rm -rf $(BUILD_DIR)

.PHONY: distclean
distclean: clean
	@echo "  [clean] remove generated build directories"
	rm -rf build-* coverage*

help: ## Show available targets
	@echo "Telos Linux convenience targets:"
	@echo "  make                 build the host (default)"
	@echo "  make build           build $(BUILD_DIR)"
	@echo "  make run             interactive terminal Agent"
	@echo "  make run PROMPT=...  one non-interactive turn"
	@echo "  make chat PROMPT=...  one-shot prompt"
	@echo "  make test        full test suite"
	@echo "  make test-unit / test-plugins / test-functional"
	@echo "  make doctor       host health check"
	@echo "  make check        source style checks"
	@echo "  make acceptance    acceptance traceability"
	@echo "  make sanitize      ASan/UBSan build and test"
	@echo "  make coverage      coverage gate"
	@echo "  make clean / distclean"
	@echo
	@echo "Overridable: BUILD_DIR MESON CC CONFIGURE_OPTS PROMPT TESTS TELOS_ENV"

.PHONY: help