# CONTRIBUTING

## Project Naming Standard (C Codebase)

### General Principles

-   Names must be consistent, readable, and meaningful.
-   Use `snake_case` for most identifiers unless explicitly listed
    otherwise.

### Variables

**Local variables** - `snake_case`

**Function parameters** - `snake_case`

**Global variables** - Must start with: `g_` - Use `snake_case` after
the prefix
Example: `g_config_table`

**Static const variables (file-scope constants)** - `UPPER_CASE`

### Constants

-   Non-static constants: `UPPER_CASE`
-   Enum constants: `UPPER_CASE`
-   Macros (`#define`): `UPPER_CASE`

### Types

-   `typedef` names: `CamelCase`
-   `struct` names: `CamelCase`
-   `union` names: `CamelCase`
-   `enum` names: `CamelCase`

Examples: - `HttpRequest` - `WeatherData` - `ConnectionState`

### Functions

-   Function names: `snake_case`
    Example: `http_client_get()`

### Readability & Style Rules

-   Always include braces for control statements, even single-line
    statements:

        if (cond) {
            ...
        }

-   Functions larger than 300 lines trigger a warning.

-   Non-const parameters that could be `const` will warn (unless
    ALL_CAPS).

### CI Behavior

-   Naming violations **fail CI**.
-   Other readability checks **warn but do not fail** the build.
