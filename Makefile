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

# Strict flags for YOUR source code (MMD/MP generate .d dependency files)
CFLAGS_STRICT := $(CFLAGS_BASE) -Wall -Wextra -MMD -MP

# Loose flags for third-party libraries (also track deps so header changes propagate)
CFLAGS_LOOSE   := -O2 -w -MMD -MP
CXXFLAGS_LOOSE := -O2 -w -std=c++11 -MMD -MP 

INCLUDES := -I$(INC_DIR) \
            $(shell find $(SRC_DIR) -type d | sed 's/^/-I/') \
            $(shell find -L $(LIB_DIR) -type d | sed 's/^/-I/')

LDFLAGS :=
LIBS := -lmbedtls -lmbedx509 -lmbedcrypto -pthread -lstdc++

# ------------------------------------------------------------
# Valgrind / Memcheck
# ------------------------------------------------------------
MEMCHECK := valgrind \
	--tool=memcheck \
	--leak-check=full \
	--show-leak-kinds=all \
	--track-origins=yes \
	--errors-for-leak-kinds=definite \
	--error-exitcode=1
# ------------------------------------------------------------
# Auto-discover binaries
# ------------------------------------------------------------
BIN_NAMES   := $(notdir $(wildcard $(BIN_DIR)/*))
ALL_TARGETS := $(addprefix $(BUILD_DIR)/,$(BIN_NAMES))

# ------------------------------------------------------------
# Shared sources (everything under src/ except src/bin/)
# ------------------------------------------------------------
CORE_SRC := $(shell find $(SRC_DIR) -type f -name '*.c' ! -path '$(BIN_DIR)/*')
CORE_OBJ := $(patsubst %.c,$(BUILD_DIR)/%.o,$(CORE_SRC))

LIB_SRC     := $(shell find -L $(LIB_DIR) -type f -name '*.c')
LIB_CPP_SRC := $(shell find -L $(LIB_DIR) -type f -name '*.cpp')

LIB_OBJ     := $(patsubst %.c,$(BUILD_DIR)/%.o,$(LIB_SRC))
LIB_CPP_OBJ := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(LIB_CPP_SRC))

COMMON_OBJ := $(CORE_OBJ) $(LIB_OBJ) $(LIB_CPP_OBJ)

# ------------------------------------------------------------
# Per-binary sources and objects
# ------------------------------------------------------------
# For each binary, define its sources and objects.
# BIN_SRC_<name> and BIN_OBJ_<name> are set dynamically.
define BINARY_template
BIN_SRC_$(1) := $$(shell find $(BIN_DIR)/$(1) -type f -name '*.c')
BIN_OBJ_$(1) := $$(patsubst %.c,$(BUILD_DIR)/%.o,$$(BIN_SRC_$(1)))
endef
$(foreach bin,$(BIN_NAMES),$(eval $(call BINARY_template,$(bin))))

# Collect ALL object files across all binaries (for dep file inclusion)
ALL_BIN_OBJ := $(foreach bin,$(BIN_NAMES),$(BIN_OBJ_$(bin)))

# ------------------------------------------------------------
# Dependency files — include ALL of them unconditionally.
# The '-' prefix suppresses errors for files that don't exist yet
# (first build). On subsequent builds, these .d files tell make
# exactly which headers each .o depends on, so any header change
# triggers the right recompilation automatically.
# ------------------------------------------------------------
ALL_OBJ  := $(COMMON_OBJ) $(ALL_BIN_OBJ)
DEP_FILES := $(ALL_OBJ:.o=.d)
-include $(DEP_FILES)

# ------------------------------------------------------------
# Default target: build everything
# ------------------------------------------------------------
.PHONY: all
all: $(ALL_TARGETS)
	@echo "Built all binaries [$(BUILD_TYPE)]"

# Rule to link each discovered binary directly (no sub-make needed)
define LINK_template
$(BUILD_DIR)/$(1): $(COMMON_OBJ) $$(BIN_OBJ_$(1))
	@mkdir -p $$(dir $$@)
	@echo "[LD]  $$@"
	@$(CXX) $(LDFLAGS) $$^ -o $$@ $(LIBS)
endef
$(foreach bin,$(BIN_NAMES),$(eval $(call LINK_template,$(bin))))

# ------------------------------------------------------------
# Single binary build  (make build BIN=foo  or  make run BIN=foo)
# ------------------------------------------------------------
ifeq ($(filter build run,$(MAKECMDGOALS)),build run)
ifndef BIN
$(error Please specify BIN=<name>. Available: $(BIN_NAMES))
endif
endif

TARGET := $(BUILD_DIR)/$(BIN)

.PHONY: build
build: $(TARGET)
	@echo "Built $(BIN) [$(BUILD_TYPE)]"

# ------------------------------------------------------------
# Compilation rules
# ------------------------------------------------------------

# ---- Strict project C compilation (src/) ----
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

.PHONY: valgrind
valgrind: all
	@echo "Running Valgrind on all binaries..."
	@for b in $(BIN_NAMES); do \
		echo ""; \
		echo "=== $$b ==="; \
		valgrind --leak-check=full --show-leak-kinds=all \
		         --track-origins=yes \
		         ./$(BUILD_DIR)/$$b; \
	done

.PHONY: memcheck
memcheck: build
	@echo "Running Memcheck on $(BIN)..."
	@$(MEMCHECK) ./$(TARGET) $(ARGS)

GPROF_OUT ?= gprof.txt
TIMEOUT   ?=

.PHONY: profile
profile:
	@echo "Building $(BIN) with gprof instrumentation (-pg)..."
	$(MAKE) clean
	$(MAKE) build BIN=$(BIN) CFLAGS_BASE="-O1 -g -pg" LDFLAGS="-pg"
	@echo "Running $(BIN)$(if $(TIMEOUT), (timeout $(TIMEOUT)s),)..."
	@if [ -n "$(TIMEOUT)" ]; then \
		timeout --signal=SIGTERM $(TIMEOUT) ./$(BUILD_DIR)/$(BIN) $(ARGS) || true; \
	else \
		./$(BUILD_DIR)/$(BIN) $(ARGS); \
	fi
	@echo "Analyzing..."
	@gprof $(BUILD_DIR)/$(BIN) gmon.out > $(GPROF_OUT)
	@echo "Report saved to $(GPROF_OUT)"

.PHONY: callgrind
callgrind: build
	@echo "Running Callgrind on $(BIN)$(if $(TIMEOUT), (timeout $(TIMEOUT)s),)..."
	@if [ -n "$(TIMEOUT)" ]; then \
		valgrind --tool=callgrind --callgrind-out-file=callgrind.out \
		  timeout --signal=SIGTERM $(TIMEOUT) ./$(TARGET) $(ARGS) || true; \
	else \
		valgrind --tool=callgrind --callgrind-out-file=callgrind.out ./$(TARGET) $(ARGS); \
	fi
	@echo "Results saved to callgrind.out"
	@echo "Visualize: kcachegrind callgrind.out  OR  callgrind_annotate callgrind.out"

# ------------------------------------------------------------
# Daemon management
# ------------------------------------------------------------
WATCHDOG := $(BUILD_DIR)/watchdog
SERVER   := $(BUILD_DIR)/server
COMPUTE  := $(BUILD_DIR)/compute

WATCHDOG_PID := /tmp/jws-watchdog.pid

# Depend on the actual binaries so make rebuilds them if sources changed
# before attempting to start the daemon.
.PHONY: daemon-start
daemon-start: $(WATCHDOG) $(SERVER) $(COMPUTE)
	@if [ -f $(WATCHDOG_PID) ]; then \
		PID=$$(cat $(WATCHDOG_PID)); \
		if kill -0 $$PID 2>/dev/null; then \
			echo "Watchdog already running (PID $$PID)"; \
			exit 1; \
		fi; \
	fi
	@echo "Starting watchdog..."
	@nohup $(WATCHDOG) \
		--server $(SERVER) \
		--compute $(COMPUTE) \
		-- $(ARGS) \
		> /tmp/watchdog.out 2>&1 & \
		echo $$! > $(WATCHDOG_PID)
	@echo "Watchdog started (PID $$(cat $(WATCHDOG_PID)))"

daemon-stop:
	@if [ -f $(WATCHDOG_PID) ]; then \
		PID=$$(cat $(WATCHDOG_PID)); \
		echo "Stopping watchdog (PID $$PID)..."; \
		kill -TERM $$PID 2>/dev/null || true; \
		rm -f $(WATCHDOG_PID); \
		for i in 1 2 3 4 5 6 7 8 9 10; do \
			kill -0 $$PID 2>/dev/null || { echo "Stopped."; exit 0; }; \
			sleep 1; \
		done; \
		echo "Watchdog still running after 10s, sending SIGKILL..."; \
		kill -KILL $$PID 2>/dev/null || true; \
	else \
		echo "Watchdog not running."; \
	fi

.PHONY: daemon-status
daemon-status:
	@if [ -f $(WATCHDOG_PID) ]; then \
		PID=$$(cat $(WATCHDOG_PID)); \
		if kill -0 $$PID 2>/dev/null; then \
			echo "Watchdog running (PID $$PID)"; \
		else \
			echo "Stale PID file."; \
		fi; \
	else \
		echo "Watchdog not running."; \
	fi

.PHONY: watchdog-foreground
watchdog-foreground: $(WATCHDOG) $(SERVER) $(COMPUTE)
	@echo "Running watchdog in foreground..."
	@$(WATCHDOG) \
		--foreground \
		--server $(SERVER) \
		--compute $(COMPUTE) \
		$(ARGS)

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
			--system-headers=false \
			-- $(INCLUDES) || true; \
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
			--system-headers=false \
			-- $(INCLUDES) || true; \
	done
	@echo "Auto-fix complete. Please review changes with 'git diff'."

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
			--system-headers=false \
			-- $(INCLUDES); then \
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

#------------------------------------------------------------------------------
# Docker for deployment
#------------------------------------------------------------------------------
.PHONY: docker-build
docker-build:
	docker build -t just-api .

.PHONY: docker-run
docker-run:
	mkdir -p energy_plan cache logs
	-docker rm -f just-api 2>/dev/null || true
	docker run -d \
		--name just-api \
		-u $$(id -u):$$(id -g) \
		-p 10680:10680 \
		-v $(shell pwd)/config.json:/app/config.json \
		-v $(shell pwd)/energy_plan:/app/energy_plan \
		-v $(shell pwd)/cache:/app/cache \
		-v $(shell pwd)/logs:/app/logs \
		just-api

.PHONY: docker-stop
docker-stop:
	docker stop just-api

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

.PHONY: docs-open
docs-open:
	@echo "Opening documentation..."
	@xdg-open documentation/html/index.html
	@echo "Documentation opened in default browser."
