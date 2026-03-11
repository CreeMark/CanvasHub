#ifndef UTILS_H
#define UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stddef.h>

// 安全的字符串复制
char *safe_strdup(const char *str);

// 安全的内存分配
void *safe_malloc(size_t size);

// 安全的内存重分配
void *safe_realloc(void *ptr, size_t size);

// 安全的内存释放
void safe_free(void **ptr);

// 字符串拼接
char *str_concat(const char *str1, const char *str2);

// 字符串格式化
char *str_printf(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif // UTILS_H
