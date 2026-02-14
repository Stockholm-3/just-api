#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include "config_types.h"

/**
 * @brief Parse configuration from file.
 *
 * Supports both JSON and simple key=value format.
 * Falls back to defaults for missing values.
 *
 * @param[in]  filepath Path to config file
 * @param[out] config   Pointer to config structure to populate
 *
 * @return 0 on success, -1 on file error, -2 on parse error
 */
int config_parser_load(const char* filepath, ServerConfig* config);

/**
 * @brief Parse configuration from command-line arguments.
 *
 * @param[in]  argc   Argument count
 * @param[in]  argv   Argument vector
 * @param[out] config Config structure to populate
 *
 * @return 0 on success, -1 on error
 */
int config_parser_from_args(int argc, char* argv[], ServerConfig* config);

/**
 * @brief Validate configuration values.
 *
 * @param[in] config Configuration to validate
 *
 * @return 0 if valid, -1 if invalid
 */
int config_parser_validate(const ServerConfig* config);

/**
 * @brief Print current configuration (for debugging).
 *
 * @param[in] config Configuration to print
 */
void config_parser_print(const ServerConfig* config);

#endif // CONFIG_PARSER_H