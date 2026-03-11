#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "logger.h"

// 安全的字符串复制
char *safe_strdup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *copy = malloc(len + 1);
    if (!copy) {
        LOG_ERROR("Failed to allocate memory for string duplication");
        return NULL;
    }
    strcpy(copy, str);
    return copy;
}

// 安全的内存分配
void *safe_malloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr && size > 0) {
        LOG_ERROR("Failed to allocate memory of size %zu", size);
    }
    return ptr;
}

// 安全的内存重分配
void *safe_realloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        LOG_ERROR("Failed to reallocate memory to size %zu", size);
    }
    return new_ptr;
}

// 安全的内存释放
void safe_free(void **ptr) {
    if (ptr && *ptr) {
        free(*ptr);
        *ptr = NULL;
    }
}

// 字符串拼接
char *str_concat(const char *str1, const char *str2) {
    if (!str1 && !str2) return NULL;
    if (!str1) return safe_strdup(str2);
    if (!str2) return safe_strdup(str1);
    
    size_t len1 = strlen(str1);
    size_t len2 = strlen(str2);
    char *result = safe_malloc(len1 + len2 + 1);
    if (!result) return NULL;
    
    strcpy(result, str1);
    strcat(result, str2);
    return result;
}

// 字符串格式化
char *str_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    // 首先计算所需的缓冲区大小
    int len = vsnprintf(NULL, 0, format, args);
    va_end(args);
    
    if (len < 0) {
        LOG_ERROR("vsnprintf failed");
        return NULL;
    }
    
    char *result = safe_malloc(len + 1);
    if (!result) return NULL;
    
    va_start(args, format);
    vsnprintf(result, len + 1, format, args);
    va_end(args);
    
    return result;
}
