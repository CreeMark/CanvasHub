#ifndef CORE_LOGIC_H
#define CORE_LOGIC_H

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化核心逻辑
void core_init(void);

// 设置网络客户端
void core_set_client(net_client_t *client);

// 设置回调函数
void core_set_callbacks(
    void (*on_login_success)(void *user_data),
    void (*on_login_failure)(const char *error, void *user_data),
    void (*on_register_success)(void *user_data),
    void (*on_register_failure)(const char *error, void *user_data),
    void (*on_room_list)(room_list_msg_t *list, void *user_data),
    void (*on_join_room_success)(void *user_data),
    void (*on_save_canvas_success)(void *user_data),
    void (*on_save_canvas_failure)(const char *error, void *user_data),
    void (*on_draw)(draw_msg_t *msg, void *user_data),
    void (*on_chat)(chat_msg_t *msg, void *user_data),
    void (*on_undo)(void *user_data),
    void (*on_redo)(void *user_data),
    void (*on_clear)(void *user_data),
    void (*on_load_canvas)(const char *data, void *user_data),
    void (*on_user_joined)(const char *username, uint32_t user_id, void *user_data),
    void (*on_chat_history)(chat_msg_t *messages, int count, void *user_data),
    void *user_data
);

// 登录
void core_login(const char *username, const char *password);

// 注册
void core_register(const char *username, const char *password, const char *email);

// 获取房间列表
void core_list_rooms(void);

// 创建房间
void core_create_room(const char *name, const char *description);

// 加入房间
void core_join_room(uint32_t room_id);

// 保存画布
void core_save_canvas(void);

// 发送绘制命令
void core_send_draw(draw_msg_t *msg);

// 发送聊天消息
void core_send_chat(const char *content);

// 发送 undo 命令
void core_send_undo(void);

// 发送 redo 命令
void core_send_redo(void);

// 发送 clear 命令
void core_send_clear(void);

// 处理网络消息
void core_handle_message(const char *msg, size_t len);

// 设置用户信息
void core_set_user_info(uint32_t user_id, const char *username);

// 获取当前房间 ID
uint32_t core_get_current_room_id(void);

// 获取用户名
const char *core_get_username(void);

#ifdef __cplusplus
}
#endif

#endif // CORE_LOGIC_H
