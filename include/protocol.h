#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 协议类型枚举
typedef enum {
    PROTO_MSG_HANDSHAKE = 0x01,
    PROTO_MSG_DRAW,
    PROTO_MSG_CHAT,
    PROTO_MSG_UNDO,
    PROTO_MSG_REDO,
    PROTO_MSG_CLEAR,
    PROTO_MSG_LOGIN = 0x10,
    PROTO_MSG_REGISTER,
    PROTO_MSG_CREATE_ROOM,
    PROTO_MSG_JOIN_ROOM,
    PROTO_MSG_LIST_ROOMS,
    PROTO_MSG_LEAVE_ROOM,
    PROTO_MSG_SAVE_CANVAS,
    PROTO_MSG_LOAD_CANVAS,
    PROTO_MSG_ERROR = 0xFF
} msg_type_t;

// Authentication Message
typedef struct {
    char username[64];
    char password[64];
    char email[128]; // Added email field
} auth_msg_t;

// Room Message
typedef struct {
    uint32_t room_id;
    char name[128];
    char description[512];
    uint32_t owner_id;
} room_msg_t;

// Project List (Response)
typedef struct {
    uint32_t count;
    room_msg_t *rooms;
} room_list_msg_t;

// Protocol Functions
char *protocol_serialize_auth(const char *type, const auth_msg_t *msg);
int protocol_deserialize_auth(const char *json, auth_msg_t *out_msg);

char *protocol_serialize_room(const char *type, const room_msg_t *msg);
int protocol_deserialize_room(const char *json, room_msg_t *out_msg);

char *protocol_serialize_room_list(const room_list_msg_t *msg);
// Note: Deserializing list requires manual array parsing or cJSON iteration, skipping full helper for brevity unless needed by client

// ... Existing ...
typedef struct {
    uint8_t version; // 0x01
    uint8_t type;    // msg_type_t
    uint16_t length; // 载荷长度
    uint32_t timestamp;
} packet_header_t;

// 绘制消息体 (JSON载荷解析后的结构)
typedef struct {
    int tool_type;
    uint32_t color;
    double width;
    struct {
        double x;
        double y;
    } *points;
    size_t point_count;
} draw_msg_t;

// 聊天消息体
typedef struct {
    char sender[64];
    char content[512];
    uint64_t timestamp;
} chat_msg_t;

// 序列化绘制消息为 JSON 字符串
char *protocol_serialize_draw(const draw_msg_t *msg);

// 反序列化 JSON 字符串为绘制消息
int protocol_deserialize_draw(const char *json, draw_msg_t *out_msg);

// 序列化聊天消息
char *protocol_serialize_chat(const chat_msg_t *msg);

// 反序列化聊天消息
int protocol_deserialize_chat(const char *json, chat_msg_t *out_msg);

// 序列化简单指令 (undo/redo/clear)
char *protocol_serialize_cmd(const char *type);

// 释放绘制消息内部资源
void protocol_free_draw_msg(draw_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif // PROTOCOL_H
