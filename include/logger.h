#ifndef LOGGER_H
#define LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

// 日志级别
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
} log_level_t;

// 初始化日志系统
void logger_init(log_level_t level, const char *log_file);

// 设置日志级别
void logger_set_level(log_level_t level);

// 日志记录函数
void logger_log(log_level_t level, const char *file, int line, const char *format, ...);

// 关闭日志系统
void logger_close(void);

// 日志宏
#define LOG_DEBUG(format, ...) logger_log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) logger_log(LOG_LEVEL_INFO, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_WARNING(format, ...) logger_log(LOG_LEVEL_WARNING, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) logger_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_FATAL(format, ...) logger_log(LOG_LEVEL_FATAL, __FILE__, __LINE__, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // LOGGER_H
