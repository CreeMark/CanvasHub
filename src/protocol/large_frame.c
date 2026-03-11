#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>

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

// 计算简单的校验和
static uint32_t calculate_checksum(const char *data, size_t len) {
    uint32_t checksum = 0;
    for (size_t i = 0; i < len; i++) {
        checksum += data[i];
    }
    return checksum;
}

// 初始化分片组装器
fragment_assembler_t *fragment_assembler_init(uint32_t message_id, uint16_t total_fragments, uint32_t total_size) {
    fragment_assembler_t *assembler = malloc(sizeof(fragment_assembler_t));
    if (!assembler) return NULL;
    
    assembler->message_id = message_id;
    assembler->total_fragments = total_fragments;
    assembler->received_fragments = 0;
    assembler->total_size = total_size;
    assembler->data = malloc(total_size);
    if (!assembler->data) {
        free(assembler);
        return NULL;
    }
    memset(assembler->received, 0, sizeof(assembler->received));
    
    return assembler;
}

// 添加分片到组装器
int fragment_assembler_add(fragment_assembler_t *assembler, uint16_t fragment_index, const char *data, uint32_t fragment_size) {
    if (!assembler || fragment_index >= assembler->total_fragments) {
        return -1;
    }
    
    if (assembler->received[fragment_index]) {
        return 0; // 已收到该分片
    }
    
    // 计算分片在总数据中的偏移量
    uint32_t offset = 0;
    for (uint16_t i = 0; i < fragment_index; i++) {
        // 假设前面的分片都是最大大小，最后一个分片可能更小
        if (i < assembler->total_fragments - 1) {
            offset += MAX_FRAGMENT_SIZE;
        } else {
            offset = assembler->total_size - fragment_size;
        }
    }
    
    // 复制分片数据
    if (offset + fragment_size <= assembler->total_size) {
        memcpy(assembler->data + offset, data, fragment_size);
        assembler->received[fragment_index] = 1;
        assembler->received_fragments++;
    }
    
    return 0;
}

// 检查组装是否完成
int fragment_assembler_is_complete(fragment_assembler_t *assembler) {
    if (!assembler) return 0;
    return assembler->received_fragments == assembler->total_fragments;
}

// 获取组装完成的数据
char *fragment_assembler_get_data(fragment_assembler_t *assembler, size_t *out_len) {
    if (!assembler || !fragment_assembler_is_complete(assembler)) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    
    if (out_len) *out_len = assembler->total_size;
    return assembler->data;
}

// 释放组装器
void fragment_assembler_free(fragment_assembler_t *assembler) {
    if (assembler) {
        if (assembler->data) {
            free(assembler->data);
        }
        free(assembler);
    }
}

// 分片发送大数据
int large_frame_send(int socket, const char *data, size_t data_len, ssize_t (*send_func)(int, const void *, size_t, int)) {
    static uint32_t message_id_counter = 0;
    uint32_t message_id = message_id_counter++;
    
    // 计算分片数
    uint16_t total_fragments = (data_len + MAX_FRAGMENT_SIZE - 1) / MAX_FRAGMENT_SIZE;
    if (total_fragments > MAX_FRAGMENTS) {
        return -1; // 分片数超过最大值
    }
    
    // 发送各个分片
    for (uint16_t i = 0; i < total_fragments; i++) {
        // 计算当前分片的大小和偏移量
        uint32_t fragment_size = MAX_FRAGMENT_SIZE;
        if (i == total_fragments - 1) {
            fragment_size = data_len % MAX_FRAGMENT_SIZE;
            if (fragment_size == 0) fragment_size = MAX_FRAGMENT_SIZE;
        }
        uint32_t offset = i * MAX_FRAGMENT_SIZE;
        
        // 构建头部
        large_frame_header_t header;
        header.message_id = message_id;
        header.total_fragments = total_fragments;
        header.fragment_index = i;
        header.total_size = data_len;
        header.fragment_size = fragment_size;
        header.checksum = calculate_checksum(data + offset, fragment_size);
        
        // 构建完整的分片数据
        size_t total_packet_size = sizeof(large_frame_header_t) + fragment_size;
        char *packet = malloc(total_packet_size);
        if (!packet) return -1;
        
        memcpy(packet, &header, sizeof(large_frame_header_t));
        memcpy(packet + sizeof(large_frame_header_t), data + offset, fragment_size);
        
        // 发送分片
        ssize_t sent = send_func(socket, packet, total_packet_size, 0);
        free(packet);
        
        if (sent != (ssize_t)total_packet_size) {
            return -1;
        }
        
        // 简单的流控：每发送一个分片，等待一段时间
        usleep(1000); // 1ms
    }
    
    return 0;
}

// 解析大帧分片
int large_frame_parse(const char *data, size_t data_len, large_frame_header_t *header, char **out_payload, size_t *out_payload_len) {
    if (data_len < sizeof(large_frame_header_t)) {
        return -1; // 数据不足
    }
    
    memcpy(header, data, sizeof(large_frame_header_t));
    
    if (data_len < sizeof(large_frame_header_t) + header->fragment_size) {
        return -2; // 分片数据不足
    }
    
    *out_payload = (char *)(data + sizeof(large_frame_header_t));
    *out_payload_len = header->fragment_size;
    
    // 验证校验和
    uint32_t checksum = calculate_checksum(*out_payload, *out_payload_len);
    if (checksum != header->checksum) {
        return -3; // 校验和错误
    }
    
    return 0;
}
