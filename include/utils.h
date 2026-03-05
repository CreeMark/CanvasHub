#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 简单的日志函数
void log_info(const char *fmt, ...);
void log_error(const char *fmt, ...);
void log_debug(const char *fmt, ...);

// 时间戳获取
uint64_t get_timestamp_ms(void);

// 字符串哈希 (SHA256 简化版，用于密码存储前的简单处理，实际应使用 OpenSSL)
// 返回 32 字节哈希值
void hash_sha256(const char *data, size_t len, uint8_t *out_hash);

// 随机数生成 (用于 Session ID)
uint32_t random_u32(void);

#ifdef __cplusplus
}
#endif

#endif // UTILS_H
