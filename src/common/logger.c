#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include "logger.h"

static log_level_t g_log_level = LOG_LEVEL_INFO;
static FILE *g_log_file = NULL;

void logger_init(log_level_t level, const char *log_file) {
    g_log_level = level;
    
    if (log_file) {
        g_log_file = fopen(log_file, "a");
        if (!g_log_file) {
            fprintf(stderr, "Failed to open log file: %s\n", log_file);
        }
    }
}

void logger_set_level(log_level_t level) {
    g_log_level = level;
}

void logger_log(log_level_t level, const char *file, int line, const char *format, ...) {
    if (level < g_log_level) {
        return;
    }
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    const char *level_str;
    switch (level) {
        case LOG_LEVEL_DEBUG:
            level_str = "DEBUG";
            break;
        case LOG_LEVEL_INFO:
            level_str = "INFO";
            break;
        case LOG_LEVEL_WARNING:
            level_str = "WARNING";
            break;
        case LOG_LEVEL_ERROR:
            level_str = "ERROR";
            break;
        case LOG_LEVEL_FATAL:
            level_str = "FATAL";
            break;
        default:
            level_str = "UNKNOWN";
            break;
    }
    
    va_list args, args_copy;
    va_start(args, format);
    va_copy(args_copy, args);
    
    fprintf(stdout, "[%s] [%s] %s:%d: ", timestamp, level_str, file, line);
    vfprintf(stdout, format, args);
    fprintf(stdout, "\n");
    fflush(stdout);
    
    if (g_log_file) {
        fprintf(g_log_file, "[%s] [%s] %s:%d: ", timestamp, level_str, file, line);
        vfprintf(g_log_file, format, args_copy);
        fprintf(g_log_file, "\n");
        fflush(g_log_file);
    }
    
    va_end(args_copy);
    va_end(args);
}

void logger_close(void) {
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}
