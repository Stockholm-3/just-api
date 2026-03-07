#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include "config_types.h"

/**
 * @brief Load configuration from a JSON file.
 *
 * Starts from defaults, then overlays values present in the file.
 * Missing keys keep their default values.
 *
 * @param filepath  Path to config JSON file.
 * @param config    Output config struct.
 * @return 0 on success, -1 on file/IO error, -2 on parse error.
 */
int config_parser_load(const char* filepath, ServerConfig* config);

/**
 * @brief Validate a loaded config.
 * @return 0 if valid, -1 otherwise (prints reason to stderr).
 */
int config_parser_validate(const ServerConfig* config);

/**
 * @brief Print config to stdout (debug).
 */
void config_parser_print(const ServerConfig* config);

#endif /* CONFIG_PARSER_H */
