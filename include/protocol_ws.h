#ifndef PROTOCOL_WS_H
#define PROTOCOL_WS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 检查是否是 WebSocket 握手请求
int ws_is_handshake(const char *request);

// 生成 WebSocket 握手响应
// out_response: 输出缓冲区
// out_len: 输出缓冲区长度 (输入/输出)
// sec_websocket_key: 从请求头中解析出的 Key
int ws_generate_handshake_response(const char *sec_websocket_key, char *out_response, size_t *out_len);

// 解析 HTTP 请求头中的 Sec-WebSocket-Key
// return: 0 on success, -1 on failure
int ws_parse_handshake_key(const char *request, char *out_key, size_t max_len);

// 简单的 WebSocket 帧构建 (Text Frame)
// out_frame: 输出缓冲区
// out_len: 输出缓冲区长度 (输入时为容量，输出时为实际长度)
// payload: 数据载荷
// payload_len: 载荷长度
int ws_build_frame(const char *payload, size_t payload_len, char *out_frame, size_t *out_len);

// 解析 WebSocket 帧
// in_frame: 输入数据
// in_len: 输入长度
// out_payload: 指向载荷的指针 (无需 free，直接指向 in_frame 内部)
// out_payload_len: 载荷长度
// return: 消耗的字节数，<0 表示错误或数据不足
int ws_parse_frame(const char *in_frame, size_t in_len, char **out_payload, size_t *out_payload_len);

#ifdef __cplusplus
}
#endif

#endif // PROTOCOL_WS_H
