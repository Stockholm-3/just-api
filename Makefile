SHELL := bash

# ------------------------------------------------------------
# Compiler + global settings
# ------------------------------------------------------------
CC          := gcc

SRC_DIR     := src
LIB_DIR     := lib
INC_DIR     := includes

BUILD_MODE  ?= debug
BUILD_DIR   := build/$(BUILD_MODE)
BIN  := $(BUILD_DIR)/just-weather-server

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
# Include directories
# ------------------------------------------------------------
SRC_INCLUDES := $(shell find $(SRC_DIR) -type d)
LIB_INCLUDES := $(shell find -L $(LIB_DIR) -type d)

# ------------------------------------------------------------
# Compiler flags
# ------------------------------------------------------------

INCLUDES := $(addprefix -I,$(SRC_INCLUDES)) $(addprefix -I,$(LIB_INCLUDES)) -I$(INC_DIR)
#POSIX_FLAGS := -D_POSIX_C_SOURCE=200809L

CFLAGS_SRC := $(CFLAGS_BASE) -Wall -Werror -Wfatal-errors -MMD -MP $(INCLUDES)
CFLAGS_LIB := $(CFLAGS_BASE) -w $(INCLUDES)

LDFLAGS :=
LIBS    :=

# ------------------------------------------------------------
# Source and object files
# ------------------------------------------------------------
SRC_FILES := $(shell find $(SRC_DIR) -type f -name '*.c')
LIB_FILES := $(shell find -L $(LIB_DIR) -type f -name '*.c' ! -path '*/weather/*')

OBJ_SRC := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRC_FILES))
OBJ_LIB := $(patsubst %.c,$(BUILD_DIR)/%.o,$(LIB_FILES))
OBJ     := $(OBJ_SRC) $(OBJ_LIB)

# ------------------------------------------------------------
# Build rules
# ------------------------------------------------------------
.PHONY: all
all: $(BIN)
	@echo "Build complete. [$(BUILD_TYPE)]"

# Link server binary
$(BIN): $(OBJ)
	@mkdir -p $(dir $@)
	@$(CC) $(LDFLAGS) $(OBJ) -o $@ $(LIBS)

# Compile project sources (strict flags)
$(BUILD_DIR)/src/%.o: src/%.c
	@echo "Compiling project $<... [$(BUILD_TYPE)]"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS_SRC) -c $< -o $@

# Compile library sources (relaxed flags)
$(BUILD_DIR)/lib/%.o: lib/%.c
	@echo "Compiling library $<... [$(BUILD_TYPE)]"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS_LIB) -c $< -o $@

# ------------------------------------------------------------
# Utilities
# ------------------------------------------------------------
.PHONY: run
run: $(BIN)
	@echo "Running $(BIN)..."
	@./$(BIN)

.PHONY: clean
clean:
	@rm -rf build
	@echo "Cleaned build artifacts."

# ------------------------------------------------------------
# Start server in detached tmux session
# ------------------------------------------------------------
.PHONY: start-server
start-server: $(BIN)
	@SESSION_NAME=just-weather-server; \
	if tmux has-session -t $$SESSION_NAME 2>/dev/null; then \
		echo "Session '$$SESSION_NAME' already exists. Attaching..."; \
		tmux attach -t $$SESSION_NAME; \
	else \
		echo "Starting server in detached tmux session '$$SESSION_NAME'..."; \
		tmux new -d -s $$SESSION_NAME './$(BIN)'; \
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