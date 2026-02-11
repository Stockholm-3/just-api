#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#define MAX_MODULES 16
#define MAX_MESSAGE 2048
#define MAX_LOG_LINE 4096
#define MAX_PATH 512

typedef struct {
    FILE*    all_file;
    FILE*    module_files[MAX_MODULES];
    char*    module_names[MAX_MODULES];
    int      module_count;
    LogLevel min_level;
    char     log_dir[MAX_PATH];
    int      initialized;
} Logger;

static Logger g_logger = {0};

static const char* g_level_names[] = {"DEBUG", "INFO", "WARN", "ERROR"};

static void get_timestamp(char* buf, size_t buf_size) {
    struct timeval tv;
    struct tm*     tm_info;

    gettimeofday(&tv, NULL);
    tm_info = localtime(&tv.tv_sec);

    size_t len = strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S", tm_info);
    snprintf(buf + len, buf_size - len, ".%03ld", tv.tv_usec / 1000);
}

static int ensure_directory(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    return mkdir(path, 0755);
}

static FILE* get_module_file(const char* module) {
    // Look for existing module file
    for (int i = 0; i < g_logger.module_count; i++) {
        if (strcmp(g_logger.module_names[i], module) == 0) {
            return g_logger.module_files[i];
        }
    }

    // Create new module file
    if (g_logger.module_count >= MAX_MODULES) {
        return NULL;
    }

    char filepath[MAX_PATH];
    char module_lower[32];

    // Convert to lowercase for filename
    size_t len = strlen(module);
    if (len >= sizeof(module_lower)) {
        len = sizeof(module_lower) - 1;
    }
    for (size_t i = 0; i < len; i++) {
        module_lower[i] = (module[i] >= 'A' && module[i] <= 'Z')
                              ? module[i] + ('a' - 'A')
                              : module[i];
    }
    module_lower[len] = '\0';

    int ret = snprintf(filepath, sizeof(filepath), "%s/%s.log",
                       g_logger.log_dir, module_lower);
    if (ret < 0 || (size_t)ret >= sizeof(filepath)) {
        return NULL;
    }

    FILE* file = fopen(filepath, "a");
    if (!file) {
        return NULL;
    }
    setvbuf(file, NULL, _IONBF, 0);

    g_logger.module_names[g_logger.module_count] = strdup(module);
    g_logger.module_files[g_logger.module_count] = file;
    g_logger.module_count++;

    return file;
}

int logger_init(const char* log_dir, LogLevel min_level) {
    if (g_logger.initialized) {
        return 0;
    }

    if (ensure_directory(log_dir) != 0) {
        fprintf(stderr, "Failed to create log directory: %s\n", log_dir);
        return -1;
    }

    strncpy(g_logger.log_dir, log_dir, sizeof(g_logger.log_dir) - 1);
    g_logger.log_dir[sizeof(g_logger.log_dir) - 1] = '\0';
    g_logger.min_level                             = min_level;

    // Open the combined log file
    char all_path[MAX_PATH];
    snprintf(all_path, sizeof(all_path), "%s/all.log", log_dir);

    g_logger.all_file = fopen(all_path, "a");
    if (!g_logger.all_file) {
        fprintf(stderr, "Failed to open log file: %s\n", all_path);
        return -1;
    }
    setvbuf(g_logger.all_file, NULL, _IONBF, 0);

    g_logger.initialized = 1;
    return 0;
}

void logger_shutdown(void) {
    if (!g_logger.initialized) {
        return;
    }

    if (g_logger.all_file) {
        fclose(g_logger.all_file);
        g_logger.all_file = NULL;
    }

    for (int i = 0; i < g_logger.module_count; i++) {
        if (g_logger.module_files[i]) {
            fclose(g_logger.module_files[i]);
        }
        if (g_logger.module_names[i]) {
            free(g_logger.module_names[i]);
        }
    }

    g_logger.module_count = 0;
    g_logger.initialized  = 0;
}

void logger_log(LogLevel level, const char* module, const char* fmt, ...) {
    if (!g_logger.initialized || level < g_logger.min_level) {
        return;
    }

    char    timestamp[32];
    char    message[MAX_MESSAGE];
    char    full_line[MAX_LOG_LINE];
    va_list args;

    get_timestamp(timestamp, sizeof(timestamp));

    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    int written =
        snprintf(full_line, sizeof(full_line), "[%s] [%-5s] [%s] %s\n",
                 timestamp, g_level_names[level], module, message);
    if (written < 0 || (size_t)written >= sizeof(full_line)) {
        full_line[sizeof(full_line) - 2] = '\n';
        full_line[sizeof(full_line) - 1] = '\0';
    }

    // Output to console
    FILE* console = (level >= LOG_WARN) ? stderr : stdout;
    fputs(full_line, console);
    fflush(console);

    // Write to combined log file
    if (g_logger.all_file) {
        fputs(full_line, g_logger.all_file);
        fflush(g_logger.all_file);
    }

    // Write to module-specific file
    FILE* module_file = get_module_file(module);
    if (module_file) {
        fputs(full_line, module_file);
        fflush(module_file);
    }
}
