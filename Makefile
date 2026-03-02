SHELL := bash

# ------------------------------------------------------------
# Toolchain
# ------------------------------------------------------------
CC  := gcc
CXX := g++

SRC_DIR := src
BIN_DIR := $(SRC_DIR)/bin
LIB_DIR := lib
INC_DIR := includes

BUILD_MODE ?= debug
BUILD_DIR  := build/$(BUILD_MODE)

# ------------------------------------------------------------
# Build configuration
# ------------------------------------------------------------
ifeq ($(BUILD_MODE),release)
    CFLAGS_BASE := -O3 -DNDEBUG
    BUILD_TYPE  := Release
else
    CFLAGS_BASE := -O1 -g
    BUILD_TYPE  := Debug
endif

# Strict flags for YOUR source code
CFLAGS_STRICT := $(CFLAGS_BASE) -Wall -Wextra -MMD -MP

# Loose flags for third-party libraries
CFLAGS_LOOSE   := -O2 -w
CXXFLAGS_LOOSE := -O2 -w -std=c++11

INCLUDES := -I$(INC_DIR) \
            $(shell find $(SRC_DIR) -type d | sed 's/^/-I/') \
            $(shell find -L $(LIB_DIR) -type d | sed 's/^/-I/')

LDFLAGS :=
LIBS := -lmbedtls -lmbedx509 -lmbedcrypto -pthread -lstdc++

# ------------------------------------------------------------
# Auto-discover binaries
# ------------------------------------------------------------
BIN_NAMES   := $(notdir $(wildcard $(BIN_DIR)/*))
ALL_TARGETS := $(addprefix $(BUILD_DIR)/,$(BIN_NAMES))

# ------------------------------------------------------------
# Shared sources (everything except src/bin)
# ------------------------------------------------------------
CORE_SRC := $(shell find $(SRC_DIR) -type f -name '*.c' ! -path '$(BIN_DIR)/*')
CORE_OBJ := $(patsubst %.c,$(BUILD_DIR)/%.o,$(CORE_SRC))

LIB_SRC     := $(shell find -L $(LIB_DIR) -type f -name '*.c')
LIB_CPP_SRC := $(shell find -L $(LIB_DIR) -type f -name '*.cpp')

LIB_OBJ     := $(patsubst %.c,$(BUILD_DIR)/%.o,$(LIB_SRC))
LIB_CPP_OBJ := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(LIB_CPP_SRC))

COMMON_OBJ := $(CORE_OBJ) $(LIB_OBJ) $(LIB_CPP_OBJ)

# ------------------------------------------------------------
# Default target: build everything
# ------------------------------------------------------------
.PHONY: all
all: $(ALL_TARGETS)
	@echo "Built all binaries [$(BUILD_TYPE)]"

# Pattern rule to build any discovered binary
$(BUILD_DIR)/%:
	@$(MAKE) --no-print-directory BIN=$* build

# ------------------------------------------------------------
# Single binary build
# ------------------------------------------------------------
ifneq ($(filter build run,$(MAKECMDGOALS)),)
ifndef BIN
$(error Please specify BIN=<name>. Available: $(BIN_NAMES))
endif
endif

TARGET := $(BUILD_DIR)/$(BIN)

BIN_SRC := $(shell find $(BIN_DIR)/$(BIN) -type f -name '*.c')
BIN_OBJ := $(patsubst %.c,$(BUILD_DIR)/%.o,$(BIN_SRC))

.PHONY: build
build: $(TARGET)
	@echo "Built $(BIN) [$(BUILD_TYPE)]"

$(TARGET): $(COMMON_OBJ) $(BIN_OBJ)
	@mkdir -p $(dir $@)
	@echo "[LD]  $(TARGET)"
	@$(CXX) $(LDFLAGS) $^ -o $@ $(LIBS)

# ------------------------------------------------------------
# Compilation rules with progress output
# ------------------------------------------------------------

# ---- Strict project compilation (src/) ----
$(BUILD_DIR)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	@echo "[CC]  $<"
	@$(CC) $(CFLAGS_STRICT) $(INCLUDES) -c $< -o $@

# ---- Loose third-party C compilation (lib/) ----
$(BUILD_DIR)/lib/%.o: lib/%.c
	@mkdir -p $(dir $@)
	@echo "[CC]  $<"
	@$(CC) $(CFLAGS_LOOSE) $(INCLUDES) -c $< -o $@

# ---- Loose third-party C++ compilation (lib/) ----
$(BUILD_DIR)/lib/%.o: lib/%.cpp
	@mkdir -p $(dir $@)
	@echo "[CXX] $<"
	@$(CXX) $(CXXFLAGS_LOOSE) $(INCLUDES) -c $< -o $@

# ------------------------------------------------------------
# Run selected binary
# ------------------------------------------------------------
ARGS ?=

.PHONY: run
run: build
	@echo "Running $(BIN)..."
	@./$(TARGET) $(ARGS)

# ------------------------------------------------------------
# Daemon management
# ------------------------------------------------------------
WATCHDOG := $(BUILD_DIR)/watchdog
SERVER   := $(BUILD_DIR)/server
COMPUTE  := $(BUILD_DIR)/compute

.PHONY: daemon-start
daemon-start: $(WATCHDOG) $(SERVER)
	@if [ -f /tmp/watchdog.pid ]; then \
		PID=$$(cat /tmp/watchdog.pid); \
		if kill -0 $$PID 2>/dev/null; then \
			echo "Watchdog already running (PID $$PID)"; \
			exit 1; \
		fi; \
	fi
	@echo "Starting watchdog..."
	@$(WATCHDOG) \
		--server $(SERVER) \
		-- $(ARGS)

.PHONY: daemon-stop
daemon-stop:
	@if [ -f /tmp/watchdog.pid ]; then \
		PID=$$(cat /tmp/watchdog.pid); \
		kill $$PID 2>/dev/null || true; \
		rm -f /tmp/watchdog.pid; \
		echo "Stopped."; \
	else \
		echo "Watchdog not running."; \
	fi

.PHONY: daemon-status
daemon-status:
	@if [ -f /tmp/watchdog.pid ]; then \
		PID=$$(cat /tmp/watchdog.pid); \
		if kill -0 $$PID 2>/dev/null; then \
			echo "Watchdog running (PID $$PID)"; \
		else \
			echo "Stale PID file."; \
		fi; \
	else \
		echo "Watchdog not running."; \
	fi

# ------------------------------------------------------------
# Utilities
# ------------------------------------------------------------
.PHONY: clean
clean:
	rm -rf build

.PHONY: list
list:
	@echo "Available binaries:"
	@for b in $(BIN_NAMES); do echo "  $$b"; done

.PHONY: format
format:
	@echo "Checking formatting..."
	@unformatted=$$(find . \( -name '*.c' -o -name '*.h' -o -name '*.cpp' \) -print0 | \
		xargs -0 clang-format -style=file -output-replacements-xml | \
		grep -c "<replacement " || true); \
	if [ $$unformatted -ne 0 ]; then \
		echo "$$unformatted file(s) need formatting"; \
		find . \( -name '*.c' -o -name '*.h' -o -name '*.cpp' \) -print0 | \
		xargs -0 clang-format -style=file -n -Werror; \
		exit 1; \
	else \
		echo "All files properly formatted"; \
	fi

# Actually fixes formatting
.PHONY: format-fix
format-fix:
	@echo "Applying clang-format..."
	find . \( -name '*.c' -o -name '*.h' -o -name '*.cpp' \) -print0 | xargs -0 clang-format -i -style=file
	@echo "Formatting applied."

.PHONY: lint
lint:
	@echo "Running clang-tidy using compile_commands.json..."
	@find src \( -name '*.c' -o -name '*.h' \) ! -path "*/jansson/*" -print0 | \
	while IFS= read -r -d '' file; do \
		echo "→ Linting $$file"; \
		clang-tidy "$$file" \
			--config-file=.clang-tidy \
			--quiet \
			--header-filter='^(src)/' \
			--system-headers=false || true; \
	done
	@echo "Lint complete (see warnings above)."

.PHONY: lint-fix
lint-fix:
	@echo "Running clang-tidy with auto-fix on src/ (excluding jansson)..."
	@find src \( -name '*.c' -o -name '*.h' \) ! -path "*/jansson/*" -print0 | \
	while IFS= read -r -d '' file; do \
		echo "→ Fixing $$file"; \
		clang-tidy "$$file" \
			--config-file=.clang-tidy \
			--fix \
			--fix-errors \
			--header-filter='src/.*\.(h|hpp)$$' \
			--system-headers=false || true; \
	done
	@echo "Auto-fix complete. Please review changes with 'git diff'."

# CI target: fails only on naming violations
.PHONY: lint-ci
lint-ci:
	@echo "Running clang-tidy for CI (naming violations = errors)..."
	@rm -f /tmp/clang-tidy-failed
	@find src \( -name '*.c' -o -name '*.h' \) ! -path "*/jansson/*" -print0 | \
	while IFS= read -r -d '' file; do \
		echo "→ Checking $$file"; \
		if ! clang-tidy "$$file" \
			--config-file=.clang-tidy \
			--quiet \
			--header-filter='^(src)/' \
			--system-headers=false; then \
			touch /tmp/clang-tidy-failed; \
		fi; \
	done
	@if [ -f /tmp/clang-tidy-failed ]; then \
		rm -f /tmp/clang-tidy-failed; \
		echo "❌ Lint failed: naming standard violations found"; \
		exit 1; \
	else \
		echo "✅ Lint passed"; \
	fi

.PHONY: install-lib
install-lib:
	git clone https://github.com/stockholm-3/lib.git ../lib

# ------------------------------------------------------------
# Documentation
# ------------------------------------------------------------
.PHONY: docs
docs:
	@echo "Generating documentation..."
	@doxygen
	@echo "Documentation generated in documentation/html/index.html"

.PHONY: docs-clean
docs-clean:
	@echo "Removing documentation..."
	@rm -rf documentation
	@echo "Documentation removed."

.PHONY : docs-open
docs-open:
	@echo "Opening documentation..."
	@xdg-open documentation/html/index.html
	@echo "Documentation opened in default browser."
