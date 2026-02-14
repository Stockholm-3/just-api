#ifndef LOGGER_H
#define LOGGER_H

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3
} LogLevel;

// Initialize logger
// log_dir - directory for log files (e.g. "/var/log/watchdog")
// min_level - minimum level for output
int logger_init(const char* log_dir, LogLevel min_level);

// Shutdown logger (close files)
void logger_shutdown(void);

// Main logging function
void logger_log(LogLevel level, const char* module, const char* fmt, ...);

// Convenience macros
#define LOG_DEBUG(module, fmt, ...)                                            \
    logger_log(LOG_DEBUG, module, fmt, ##__VA_ARGS__)
#define LOG_INFO(module, fmt, ...)                                             \
    logger_log(LOG_INFO, module, fmt, ##__VA_ARGS__)
#define LOG_WARN(module, fmt, ...)                                             \
    logger_log(LOG_WARN, module, fmt, ##__VA_ARGS__)
#define LOG_ERROR(module, fmt, ...)                                            \
    logger_log(LOG_ERROR, module, fmt, ##__VA_ARGS__)

#endif
