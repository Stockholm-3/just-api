SHELL := bash
# ------------------------------------------------------------
# Compiler + global settings
# ------------------------------------------------------------
CC          := gcc
SRC_DIR     := src
# Separate build folders per mode
BUILD_MODE  ?= debug
BUILD_DIR   := build/$(BUILD_MODE)
BIN_SERVER  := $(BUILD_DIR)/server/just-weather

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


# ------------------------------------------------------------
# Compiler and linker flags
# ------------------------------------------------------------
CFLAGS      := $(CFLAGS_BASE) -Wall -Werror -Wfatal-errors -MMD -MP \
               -Ilib/jansson -Isrc/lib -Isrc/server -Iincludes \
               -Isrc/lib/tcp -Isrc/lib/http -Isrc/lib/http/http_server -Isrc/lib/utils -Isrc/lib/weather \
               -Isrc/server/api -Isrc/server/api/geocoding -Isrc/server/api/openmeteo

JANSSON_CFLAGS := $(filter-out -Werror -Wfatal-errors,$(CFLAGS)) -w

LDFLAGS     := -flto -Wl,--gc-sections
LIBS        := 

# ------------------------------------------------------------
# Source and object files
# ------------------------------------------------------------
SRC_SERVER := $(shell find -L $(SRC_DIR)/server -type f -name '*.c' ! -path "*/jansson/*") \
              $(shell find -L $(SRC_DIR)/lib -type f -name '*.c' ! -path "*/jansson/*")

OBJ_SERVER  := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC_SERVER))

DEP_SERVER  := $(OBJ_SERVER:.o=.d)

# ------------------------------------------------------------
# Jansson integration
# ------------------------------------------------------------
JANSSON_SRC := $(shell find lib/jansson/ -maxdepth 1 -type f -name '*.c')
JANSSON_OBJ := $(patsubst lib/jansson/%.c,$(BUILD_DIR)/jansson/%.o,$(JANSSON_SRC))
OBJ_SERVER  += $(JANSSON_OBJ)

# ------------------------------------------------------------
# Build rules
# ------------------------------------------------------------
.PHONY: all
all: $(BIN_SERVER) $(BIN_CLIENT)
	@echo "Build complete. [$(BUILD_TYPE)]"

$(BIN_SERVER): $(OBJ_SERVER)
	@$(CC) $(LDFLAGS) $(OBJ_SERVER) -o $@ $(LIBS)

$(BIN_CLIENT): $(OBJ_CLIENT)
	@$(CC) $(LDFLAGS) $(OBJ_CLIENT) -o $@ $(LIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<... [$(BUILD_TYPE)]"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/jansson/%.o: lib/jansson/%.c
	@echo "Compiling Jansson $<... [$(BUILD_TYPE)]"
	@mkdir -p $(dir $@)
	@$(CC) $(JANSSON_CFLAGS) -c $< -o $@

# ------------------------------------------------------------
# Release target
# ------------------------------------------------------------
.PHONY: release
release:
	@$(MAKE) --no-print-directory BUILD_MODE=release all

# ------------------------------------------------------------
# Debugging and Sanitizers
# ------------------------------------------------------------
.PHONY: debug
debug:
	@$(MAKE) --no-print-directory BUILD_MODE=debug all

# Run server under GDB
.PHONY: gdb-server
gdb-server: $(BIN_SERVER)
	@echo "Launching server in GDB..."
	@gdb --quiet --args ./$(BIN_SERVER)

# Run client under GDB
.PHONY: gdb-client
gdb-client: $(BIN_CLIENT)
	@echo "Launching client in GDB..."
	@gdb --quiet --args ./$(BIN_CLIENT)

# AddressSanitizer build (for memory debugging)
.PHONY: asan
asan:
	@echo "Building with AddressSanitizer..."
	@$(MAKE) --no-print-directory \
		CFLAGS_BASE="-g -O1 -fsanitize=address -fno-omit-frame-pointer" \
		LDFLAGS="-fsanitize=address" \
		all

# ------------------------------------------------------------
# Utilities
# ------------------------------------------------------------
.PHONY: run-server
run-server: $(BIN_SERVER)
	./$(BIN_SERVER)

.PHONY: run-client
run-client: $(BIN_CLIENT)
	./$(BIN_CLIENT)

.PHONY: clean
clean:
	@rm -rf build
	@echo "Cleaned build artifacts."


# ------------------------------------------------------------
# Start server in detached tmux session
# ------------------------------------------------------------
.PHONY: start-server
start-server: $(BIN_SERVER)
	@SESSION_NAME=just-weather-server; \
	if tmux has-session -t $$SESSION_NAME 2>/dev/null; then \
		echo "Session '$$SESSION_NAME' already exists. Attaching..."; \
		tmux attach -t $$SESSION_NAME; \
	else \
		echo "Starting server in detached tmux session '$$SESSION_NAME'..."; \
		tmux new -d -s $$SESSION_NAME './$(BIN_SERVER)'; \
		echo "Server started in tmux session '$$SESSION_NAME'."; \
	fi

# Show formatting errors without modifying files
.PHONY: format
format:
	@echo "Checking formatting..."
	@unformatted=$$(find . \( -name '*.c' -o -name '*.h' \) -print0 | \
		xargs -0 clang-format -style=file -output-replacements-xml | \
		grep -c "<replacement " || true); \
	if [ $$unformatted -ne 0 ]; then \
		echo "$$unformatted file(s) need formatting"; \
		find . \( -name '*.c' -o -name '*.h' \) -print0 | \
		xargs -0 clang-format -style=file -n -Werror; \
		exit 1; \
	else \
		echo "All files properly formatted"; \
	fi

# Actually fixes formatting
.PHONY: format-fix
format-fix:
	@echo "Applying clang-format..."
	find . \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 clang-format -i -style=file
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

.PHONY: install-jansson
install-jansson:
	git clone --branch lib --single-branch https://github.com/stockholm-3/just-weather.git ../lib

# ------------------------------------------------------------
# GitHub Actions (act)
# ------------------------------------------------------------
.PHONY: workflow-build
workflow-build:
	DOCKER_HOST="$${DOCKER_HOST}" act push --job build \
       -P ubuntu-latest=catthehacker/ubuntu:act-latest

.PHONY: workflow-format
workflow-format:
	DOCKER_HOST="$${DOCKER_HOST}" act push --job format-check \
       -P ubuntu-latest=teeks99/clang-ubuntu:19

.PHONY: workflow
workflow: workflow-build workflow-format

# ------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------
-include $(DEP_SERVER)
