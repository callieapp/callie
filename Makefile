# Everyday commands. The CMake presets do the real work; this file gives them
# short names. Run `make` to list them.

PRESET ?= dev
BUILD := build/$(PRESET)
GUI := $(BUILD)/src/gui/callie-gui
CLI := $(BUILD)/src/cli/callie
GALLERY := $(BUILD)/src/gallery/callie-gallery

# Fedora's Qt sends logs to the journal when stderr is not a terminal, and its
# qtlogging.ini turns debug output off. This puts everything back on stderr,
# timestamped, with Callie's own categories fully enabled.
DEV_ENV := QT_FORCE_STDERR_LOGGING=1 \
	QT_LOGGING_RULES="qml.debug=true;callie.*=true" \
	QT_MESSAGE_PATTERN="%{time hh:mm:ss.zzz} %{if-category}%{category} %{endif}%{type}: %{message}"

FEDORA_DEPS := gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtdeclarative-devel \
	qt6-qtquickcontrols2-devel qt6-qtnetworkauth-devel qtkeychain-qt6-devel \
	tomlplusplus-devel clang-tools-extra entr nodejs npm
DEBIAN_DEPS := build-essential cmake ninja-build qt6-base-dev qt6-declarative-dev \
	qt6-declarative-dev-tools qml6-module-qtquick-controls qml6-module-qtquick-templates \
	qt6-networkauth-dev qtkeychain-qt6-dev libtomlplusplus-dev libqt6sql6-sqlite clang-format entr \
	nodejs npm

# Files whose change should rebuild and restart the app under `make watch`.
WATCHED := git ls-files --cached --others --exclude-standard -- src themes CMakeLists.txt

# Rebuilds target $(1) and restarts $(2) on every save. Quitting the app or Ctrl-C
# ends the watch; a failed build or a crash waits for the next save instead. entr -d
# exits with status 2 when a file is added, so the loop re-lists and picks it up.
define watch
	@command -v entr >/dev/null || { echo "make $@ needs entr: run make deps" >&2; exit 1; }
	@status=2; while [ $$status -eq 2 ]; do \
		$(WATCHED) | entr -d -r -z -s 'cmake --build --preset $(PRESET) --target $(1) || exec sleep infinity; \
			env $(DEV_ENV) $(2) || { echo "$(1) exited with status $$?, waiting for a change" >&2; exec sleep infinity; }'; \
		status=$$?; \
	done
endef

.DEFAULT_GOAL := help
.PHONY: help deps setup configure build run cli gallery watch watch-gallery test lint format check clean

help: ## List commands
	@awk 'BEGIN {FS = ":.*## "} /^[a-z-]+:.*## / {printf "  make %-14s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

deps: ## Install build and development dependencies (uses sudo)
	@if command -v dnf >/dev/null; then sudo dnf install -y $(FEDORA_DEPS); \
	elif command -v apt-get >/dev/null; then sudo apt-get update && sudo apt-get install -y $(DEBIAN_DEPS); \
	else echo "make deps: unsupported distro, see README.md for packages" >&2; exit 1; fi

setup: ## Install dev tooling, enable git hooks, and configure
	@npm ci
	@git config core.hooksPath .githooks
	@cmake --preset $(PRESET)

$(BUILD)/build.ninja:
	@cmake --preset $(PRESET)

configure: ## Re-run CMake, e.g. after adding google-oauth-credentials.json
	@cmake --preset $(PRESET)

build: $(BUILD)/build.ninja ## Build everything
	@cmake --build --preset $(PRESET)

run: build ## Run the app with logging on; ARGS="--theme path" passes options
	@$(DEV_ENV) $(GUI) $(ARGS)

cli: build ## Run the CLI, e.g. make cli ARGS="accounts"
	@$(DEV_ENV) $(CLI) $(ARGS)

gallery: build ## Open the theme gallery; THEME=path shows a theme file live
	@$(DEV_ENV) $(GALLERY) $(if $(THEME),--theme $(THEME))

watch: $(BUILD)/build.ninja ## Rebuild and restart the app on every save
	$(call watch,callie-gui,$(GUI) $(ARGS))

watch-gallery: $(BUILD)/build.ninja ## Like watch, for the gallery; THEME=path optional
	$(call watch,callie-gallery,$(GALLERY) $(if $(THEME),--theme $(THEME)))

test: build ## Run the tests
	@ctest --preset $(PRESET)

lint: build ## Lint QML and Markdown
	@cmake --build --preset $(PRESET) --target all_qmllint
	@npx markdownlint-cli2

format: ## Format C++, QML, Markdown, JSON and YAML in place
	@scripts/format.sh fix
	@npm run --silent format:md

check: ## Run everything CI checks: format, lint, a -Werror build, and tests
	@scripts/format.sh check
	@npx markdownlint-cli2
	@npm run --silent format:md:check
	@cmake --workflow --preset ci

clean: ## Remove build output
	@rm -rf build
