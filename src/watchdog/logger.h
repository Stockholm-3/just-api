#ifndef LOGGER_H
#define LOGGER_H

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3
} LogLevel;

// Ініціалізація логера
// log_dir - директорія для файлів логів (наприклад "/var/log/watchdog")
// min_level - мінімальний рівень для виводу
int logger_init(const char* log_dir, LogLevel min_level);

// Завершення роботи логера (закриття файлів)
void logger_shutdown(void);

// Основна функція логування
void logger_log(LogLevel level, const char* module, const char* fmt, ...);

// Зручні макроси
#define LOG_DEBUG(module, fmt, ...) logger_log(LOG_DEBUG, module, fmt, ##__VA_ARGS__)
#define LOG_INFO(module, fmt, ...)  logger_log(LOG_INFO,  module, fmt, ##__VA_ARGS__)
#define LOG_WARN(module, fmt, ...)  logger_log(LOG_WARN,  module, fmt, ##__VA_ARGS__)
#define LOG_ERROR(module, fmt, ...) logger_log(LOG_ERROR, module, fmt, ##__VA_ARGS__)

#endif
