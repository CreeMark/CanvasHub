#include "canvas.h"
#include "network.h"
#include "protocol.h"
#include "cJSON.h"
#include "undo_redo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

// 核心逻辑结构体
typedef struct {
    net_client_t *client;
    uint32_t user_id;
    uint32_t current_room_id;
    char username[64];
    // 回调函数
    void (*on_login_success)(void *user_data);
    void (*on_login_failure)(const char *error, void *user_data);
    void (*on_register_success)(void *user_data);
    void (*on_register_failure)(const char *error, void *user_data);
    void (*on_room_list)(room_list_msg_t *list, void *user_data);
    void (*on_join_room_success)(void *user_data);
    void (*on_save_canvas_success)(void *user_data);
    void (*on_save_canvas_failure)(const char *error, void *user_data);
    void (*on_draw)(draw_msg_t *msg, void *user_data);
    void (*on_chat)(chat_msg_t *msg, void *user_data);
    void (*on_undo)(void *user_data);
    void (*on_redo)(void *user_data);
    void (*on_clear)(void *user_data);
    void (*on_load_canvas)(const char *data, void *user_data);
    void (*on_user_joined)(const char *username, uint32_t user_id, void *user_data);
    void (*on_chat_history)(chat_msg_t *messages, int count, void *user_data);
    void *user_data;
} core_logic_t;

static core_logic_t g_core = {0};

// 初始化核心逻辑
void core_init(void) {
    memset(&g_core, 0, sizeof(g_core));
}

// 设置网络客户端
void core_set_client(net_client_t *client) {
    g_core.client = client;
}

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
) {
    g_core.on_login_success = on_login_success;
    g_core.on_login_failure = on_login_failure;
    g_core.on_register_success = on_register_success;
    g_core.on_register_failure = on_register_failure;
    g_core.on_room_list = on_room_list;
    g_core.on_join_room_success = on_join_room_success;
    g_core.on_save_canvas_success = on_save_canvas_success;
    g_core.on_save_canvas_failure = on_save_canvas_failure;
    g_core.on_draw = on_draw;
    g_core.on_chat = on_chat;
    g_core.on_undo = on_undo;
    g_core.on_redo = on_redo;
    g_core.on_clear = on_clear;
    g_core.on_load_canvas = on_load_canvas;
    g_core.on_user_joined = on_user_joined;
    g_core.on_chat_history = on_chat_history;
    g_core.user_data = user_data;
}

// 登录
void core_login(const char *username, const char *password) {
    auth_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.username, username, sizeof(msg.username)-1);
    strncpy(msg.password, password, sizeof(msg.password)-1);
    
    char *json = protocol_serialize_auth("login", &msg);
    if (json) {
        g_print("Sending login request for user: %s\n", username);
        if (net_client_send(g_core.client, json, strlen(json)) < 0) {
            if (g_core.on_login_failure) {
                g_core.on_login_failure("Send Failed: No Connection", g_core.user_data);
            }
        }
        free(json);
    }
}

// 注册
void core_register(const char *username, const char *password, const char *email) {
    auth_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.username, username, sizeof(msg.username)-1);
    strncpy(msg.password, password, sizeof(msg.password)-1);
    strncpy(msg.email, email, sizeof(msg.email)-1);
    
    char *json = protocol_serialize_auth("register", &msg);
    if (json) {
        g_print("Sending register request for user: %s, email: %s\n", username, email);
        if (net_client_send(g_core.client, json, strlen(json)) < 0) {
            if (g_core.on_register_failure) {
                g_core.on_register_failure("Send Failed: No Connection", g_core.user_data);
            }
        }
        free(json);
    }
}

// 获取房间列表
void core_list_rooms(void) {
    char *json = protocol_serialize_cmd("list_rooms");
    if (json) {
        net_client_send(g_core.client, json, strlen(json));
        free(json);
    }
}

// 创建房间
void core_create_room(const char *name, const char *description) {
    room_msg_t msg;
    msg.room_id = 0;
    strncpy(msg.name, name, sizeof(msg.name)-1);
    strncpy(msg.description, description ? description : "New Room", sizeof(msg.description)-1);
    msg.owner_id = g_core.user_id;
    
    char *json = protocol_serialize_room("create_room", &msg);
    if (json) {
        net_client_send(g_core.client, json, strlen(json));
        free(json);
    }
}

// 加入房间
void core_join_room(uint32_t room_id) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "join_room");
    cJSON *d = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", d);
    cJSON_AddNumberToObject(d, "room_id", room_id);
    char *json = cJSON_PrintUnformatted(root);
    net_client_send(g_core.client, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    
    g_core.current_room_id = room_id;
}

// 保存画布
void core_save_canvas(void) {
    g_print("DEBUG: core_save_canvas - starting\n");
    g_print("DEBUG: g_core.client = %p\n", (void*)g_core.client);
    
    if (!g_core.client) {
        g_print("ERROR: core_save_canvas - client is NULL\n");
        return;
    }
    
    g_print("DEBUG: Calling undo_redo_serialize...\n");
    char *data = undo_redo_serialize();
    g_print("DEBUG: undo_redo_serialize returned %p\n", (void*)data);
    
    if (!data) {
        g_print("ERROR: core_save_canvas - serialization failed\n");
        return;
    }
    
    g_print("INFO: core_save_canvas - serialized data length: %zu\n", strlen(data));
    
    g_print("DEBUG: Creating JSON object...\n");
    cJSON *root = cJSON_CreateObject();
    g_print("DEBUG: cJSON_CreateObject returned %p\n", (void*)root);
    
    if (!root) {
        g_print("ERROR: core_save_canvas - failed to create JSON object\n");
        free(data);
        return;
    }
    
    g_print("DEBUG: Adding type field...\n");
    cJSON_AddStringToObject(root, "type", "save_canvas");
    g_print("DEBUG: Adding data field...\n");
    cJSON_AddStringToObject(root, "data", data);
    
    g_print("DEBUG: Printing JSON...\n");
    char *json = cJSON_PrintUnformatted(root);
    g_print("DEBUG: cJSON_PrintUnformatted returned %p\n", (void*)json);
    
    if (json) {
        g_print("INFO: core_save_canvas - sending %zu bytes\n", strlen(json));
        g_print("DEBUG: Calling net_client_send...\n");
        if (net_client_send(g_core.client, json, strlen(json)) < 0) {
            g_print("ERROR: core_save_canvas - send failed\n");
        }
        g_print("DEBUG: net_client_send returned\n");
        free(json);
    }
    g_print("DEBUG: Deleting JSON root...\n");
    cJSON_Delete(root);
    g_print("DEBUG: Freeing data...\n");
    free(data);
    g_print("DEBUG: core_save_canvas completed\n");
}

// 发送绘制命令
void core_send_draw(draw_msg_t *msg) {
    char *json = protocol_serialize_draw(msg);
    if (json) {
        net_client_send(g_core.client, json, strlen(json));
        free(json);
    }
}

// 发送聊天消息
void core_send_chat(const char *content) {
    chat_msg_t msg;
    strncpy(msg.sender, g_core.username, sizeof(msg.sender)-1);
    strncpy(msg.content, content, sizeof(msg.content)-1);
    msg.timestamp = 0;
    
    char *json = protocol_serialize_chat(&msg);
    if (json) {
        net_client_send(g_core.client, json, strlen(json));
        free(json);
    }
}

static void send_canvas_state(const char *cmd_type) {
    char *data = undo_redo_serialize();
    if (!data) {
        g_print("ERROR: send_canvas_state - serialization failed\n");
        return;
    }
    
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(data);
        return;
    }
    
    cJSON_AddStringToObject(root, "type", cmd_type);
    cJSON_AddStringToObject(root, "data", data);
    cJSON_AddBoolToObject(root, "sync_required", 1);
    
    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        net_client_send(g_core.client, json, strlen(json));
        free(json);
    }
    
    cJSON_Delete(root);
    free(data);
}

void core_send_undo(void) {
    send_canvas_state("undo");
}

void core_send_redo(void) {
    send_canvas_state("redo");
}

void core_send_clear(void) {
    send_canvas_state("clear");
}

// 处理网络消息
void core_handle_message(const char *msg, size_t len) {
    cJSON *root = cJSON_Parse(msg);
    if (!root) return;

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) {
        cJSON_Delete(root);
        return;
    }

    // 处理操作ID，确保操作顺序
    uint64_t operation_id = 0;
    cJSON *op_id = cJSON_GetObjectItem(root, "operation_id");
    if (op_id && cJSON_IsNumber(op_id)) {
        operation_id = (uint64_t)op_id->valuedouble;
    }

    // 只处理比最后操作ID大的消息，确保操作顺序
    static uint64_t last_processed_operation_id = 0;
    if (operation_id > 0 && operation_id <= last_processed_operation_id) {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type->valuestring, "login_resp") == 0) {
        cJSON *status = cJSON_GetObjectItem(root, "status");
        if (status && strcmp(status->valuestring, "ok") == 0) {
            if (g_core.on_login_success) {
                g_core.on_login_success(g_core.user_data);
            }
        } else {
            cJSON *message = cJSON_GetObjectItem(root, "message");
            const char *err_msg = message && message->valuestring ? message->valuestring : "Login Failed";
            if (g_core.on_login_failure) {
                g_core.on_login_failure(err_msg, g_core.user_data);
            }
        }
    } else if (strcmp(type->valuestring, "register_resp") == 0) {
        cJSON *status = cJSON_GetObjectItem(root, "status");
        if (status && strcmp(status->valuestring, "ok") == 0) {
            if (g_core.on_register_success) {
                g_core.on_register_success(g_core.user_data);
            }
        } else {
            cJSON *message = cJSON_GetObjectItem(root, "message");
            const char *err_msg = message && message->valuestring ? message->valuestring : "Registration Failed";
            if (g_core.on_register_failure) {
                g_core.on_register_failure(err_msg, g_core.user_data);
            }
        }
    } else if (strcmp(type->valuestring, "room_list") == 0) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data && cJSON_IsArray(data)) {
            int count = cJSON_GetArraySize(data);
            room_list_msg_t list;
            list.count = count;
            list.rooms = malloc(sizeof(room_msg_t) * count);
            
            for (int i = 0; i < count; i++) {
                cJSON *item = cJSON_GetArrayItem(data, i);
                if (item) {
                    cJSON *id = cJSON_GetObjectItem(item, "id");
                    cJSON *name = cJSON_GetObjectItem(item, "name");
                    cJSON *owner_id = cJSON_GetObjectItem(item, "owner_id");
                    cJSON *description = cJSON_GetObjectItem(item, "description");
                    
                    if (id && name) {
                        list.rooms[i].room_id = (uint32_t)id->valuedouble;
                        list.rooms[i].owner_id = owner_id ? (uint32_t)owner_id->valuedouble : 0;
                        strncpy(list.rooms[i].name, name->valuestring, sizeof(list.rooms[i].name)-1);
                        if (description) {
                            strncpy(list.rooms[i].description, description->valuestring, sizeof(list.rooms[i].description)-1);
                        } else {
                            list.rooms[i].description[0] = '\0';
                        }
                    }
                }
            }
            
            if (g_core.on_room_list) {
                g_core.on_room_list(&list, g_core.user_data);
            }
            
            free(list.rooms);
        }
    } else if (strcmp(type->valuestring, "join_room_resp") == 0) {
        cJSON *status = cJSON_GetObjectItem(root, "status");
        if (status && strcmp(status->valuestring, "ok") == 0) {
            if (g_core.on_join_room_success) {
                g_core.on_join_room_success(g_core.user_data);
            }
        }
    } else if (strcmp(type->valuestring, "save_canvas_resp") == 0) {
        cJSON *status = cJSON_GetObjectItem(root, "status");
        cJSON *message = cJSON_GetObjectItem(root, "message");
        
        if (status && strcmp(status->valuestring, "ok") == 0) {
            if (g_core.on_save_canvas_success) {
                g_core.on_save_canvas_success(g_core.user_data);
            }
        } else {
            const char *err_msg = message && message->valuestring ? message->valuestring : "Save Failed";
            if (g_core.on_save_canvas_failure) {
                g_core.on_save_canvas_failure(err_msg, g_core.user_data);
            }
        }
    } else if (strcmp(type->valuestring, "draw") == 0) {
        draw_msg_t draw_msg;
        if (protocol_deserialize_draw(msg, &draw_msg) == 0) {
            if (g_core.on_draw) {
                g_core.on_draw(&draw_msg, g_core.user_data);
            }
            protocol_free_draw_msg(&draw_msg);
            // 更新最后处理的操作ID
            if (operation_id > 0) {
                last_processed_operation_id = operation_id;
            }
        }
    } else if (strcmp(type->valuestring, "chat") == 0) {
        chat_msg_t chat_msg;
        if (protocol_deserialize_chat(msg, &chat_msg) == 0) {
            if (g_core.on_chat) {
                g_core.on_chat(&chat_msg, g_core.user_data);
            }
            // 更新最后处理的操作ID
            if (operation_id > 0) {
                last_processed_operation_id = operation_id;
            }
        }
    } else if (strcmp(type->valuestring, "undo") == 0) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data && cJSON_IsString(data)) {
            if (g_core.on_load_canvas) {
                g_core.on_load_canvas(data->valuestring, g_core.user_data);
            }
        }
        if (operation_id > 0) {
            last_processed_operation_id = operation_id;
        }
    } else if (strcmp(type->valuestring, "redo") == 0) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data && cJSON_IsString(data)) {
            if (g_core.on_load_canvas) {
                g_core.on_load_canvas(data->valuestring, g_core.user_data);
            }
        }
        if (operation_id > 0) {
            last_processed_operation_id = operation_id;
        }
    } else if (strcmp(type->valuestring, "clear") == 0) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data && cJSON_IsString(data)) {
            if (g_core.on_load_canvas) {
                g_core.on_load_canvas(data->valuestring, g_core.user_data);
            }
        }
        if (operation_id > 0) {
            last_processed_operation_id = operation_id;
        }
    } else if (strcmp(type->valuestring, "load_canvas") == 0) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data && cJSON_IsString(data)) {
            if (g_core.on_load_canvas) {
                g_core.on_load_canvas(data->valuestring, g_core.user_data);
            }
        }
    } else if (strcmp(type->valuestring, "user_joined") == 0) {
        cJSON *username = cJSON_GetObjectItem(root, "username");
        cJSON *user_id = cJSON_GetObjectItem(root, "user_id");
        if (username && cJSON_IsString(username) && user_id && cJSON_IsNumber(user_id)) {
            if (g_core.on_user_joined) {
                g_core.on_user_joined(username->valuestring, (uint32_t)user_id->valuedouble, g_core.user_data);
            }
        }
    } else if (strcmp(type->valuestring, "chat_history") == 0) {
        cJSON *messages = cJSON_GetObjectItem(root, "messages");
        if (messages && cJSON_IsArray(messages)) {
            int count = cJSON_GetArraySize(messages);
            if (count > 0 && g_core.on_chat_history) {
                chat_msg_t *chat_msgs = malloc(sizeof(chat_msg_t) * count);
                if (chat_msgs) {
                    for (int i = 0; i < count; i++) {
                        cJSON *item = cJSON_GetArrayItem(messages, i);
                        if (item) {
                            cJSON *sender = cJSON_GetObjectItem(item, "sender");
                            cJSON *content = cJSON_GetObjectItem(item, "content");
                            cJSON *timestamp = cJSON_GetObjectItem(item, "timestamp");
                            
                            memset(&chat_msgs[i], 0, sizeof(chat_msg_t));
                            if (sender && cJSON_IsString(sender)) {
                                strncpy(chat_msgs[i].sender, sender->valuestring, sizeof(chat_msgs[i].sender) - 1);
                            }
                            if (content && cJSON_IsString(content)) {
                                strncpy(chat_msgs[i].content, content->valuestring, sizeof(chat_msgs[i].content) - 1);
                            }
                            if (timestamp && cJSON_IsNumber(timestamp)) {
                                chat_msgs[i].timestamp = (uint64_t)timestamp->valuedouble;
                            }
                        }
                    }
                    g_core.on_chat_history(chat_msgs, count, g_core.user_data);
                    free(chat_msgs);
                }
            }
        }
    }

    cJSON_Delete(root);
}

// 设置用户信息
void core_set_user_info(uint32_t user_id, const char *username) {
    g_core.user_id = user_id;
    if (username) {
        strncpy(g_core.username, username, sizeof(g_core.username)-1);
    }
}

// 获取当前房间 ID
uint32_t core_get_current_room_id(void) {
    return g_core.current_room_id;
}

// 获取用户名
const char *core_get_username(void) {
    return g_core.username;
}
