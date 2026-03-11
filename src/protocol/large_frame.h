#ifndef LARGE_FRAME_H
#define LARGE_FRAME_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_FRAGMENT_SIZE 8192
#define MAX_FRAGMENTS 128

// 大帧头部结构
typedef struct {
    uint32_t message_id;      // 消息ID，用于标识同一消息的不同分片
    uint16_t total_fragments; // 总分片数
    uint16_t fragment_index;  // 当前分片索引
    uint32_t total_size;      // 总数据大小
    uint32_t fragment_size;   // 当前分片大小
    uint32_t checksum;        // 校验和
} large_frame_header_t;

// 分片组装状态
typedef struct {
    uint32_t message_id;
    uint16_t total_fragments;
    uint16_t received_fragments;
    uint32_t total_size;
    char *data;
    uint8_t received[MAX_FRAGMENTS]; // 标记哪些分片已收到
} fragment_assembler_t;

// 初始化分片组装器
fragment_assembler_t *fragment_assembler_init(uint32_t message_id, uint16_t total_fragments, uint32_t total_size);

// 添加分片到组装器
int fragment_assembler_add(fragment_assembler_t *assembler, uint16_t fragment_index, const char *data, uint32_t fragment_size);

// 检查组装是否完成
int fragment_assembler_is_complete(fragment_assembler_t *assembler);

// 获取组装完成的数据
char *fragment_assembler_get_data(fragment_assembler_t *assembler, size_t *out_len);

// 释放组装器
void fragment_assembler_free(fragment_assembler_t *assembler);

// 分片发送大数据
int large_frame_send(int socket, const char *data, size_t data_len, ssize_t (*send_func)(int, const void *, size_t, int));

// 解析大帧分片
int large_frame_parse(const char *data, size_t data_len, large_frame_header_t *header, char **out_payload, size_t *out_payload_len);

#ifdef __cplusplus
}
#endif

#endif // LARGE_FRAME_H
