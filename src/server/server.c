#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include "protocol.h"
#include "protocol_ws.h"
#include "database.h"
#include "cJSON.h"
#include "thread_pool.h"
#include "protocol/large_frame.h"
#include "logger.h"

#define MAX_EVENTS 64
#define PORT 8080
#define BUFFER_SIZE 65536

typedef struct {
    int fd;
    uint32_t user_id;
    uint32_t room_id;
    char username[64];
    char buffer[BUFFER_SIZE];
    int buf_len;
    int is_handshaked;
    fragment_assembler_t *assembler; // 分片组装器
    uint64_t last_operation_id; // 最后处理的操作ID
} client_state_t;

// 房间状态结构体
typedef struct {
    uint32_t room_id;
    uint64_t next_operation_id; // 下一个操作的ID
    char *canvas_data; // 当前画布状态
    pthread_mutex_t mutex; // 房间锁
} room_state_t;

static room_state_t *rooms[MAX_EVENTS]; // 房间状态数组
static pthread_mutex_t rooms_mutex = PTHREAD_MUTEX_INITIALIZER;

// 任务参数结构体
typedef struct {
    int client_fd;
    uint32_t room_id;
    char *message;
    size_t message_len;
    uint64_t operation_id; // 操作ID
} broadcast_task_t;

typedef struct {
    int client_fd;
    uint32_t room_id;
    char *canvas_data;
} save_canvas_task_t;

typedef struct {
    int client_fd;
    char *username;
    char *password;
} login_task_t;

typedef struct {
    int client_fd;
    char *username;
    char *password;
    char *email;
} register_task_t;

static client_state_t clients[MAX_EVENTS];

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void init_clients() {
    for (int i = 0; i < MAX_EVENTS; i++) {
        clients[i].fd = -1;
        clients[i].user_id = 0;
        clients[i].room_id = 0;
        clients[i].buf_len = 0;
        clients[i].is_handshaked = 0;
        clients[i].assembler = NULL;
        clients[i].last_operation_id = 0;
        memset(clients[i].username, 0, sizeof(clients[i].username));
    }
}

static int add_client(int fd) {
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (clients[i].fd == -1) {
            clients[i].fd = fd;
            clients[i].user_id = 0;
            clients[i].room_id = 0;
            clients[i].buf_len = 0;
            clients[i].is_handshaked = 0;
            clients[i].assembler = NULL;
            clients[i].last_operation_id = 0;
            return i;
        }
    }
    return -1;
}

static void remove_client(int fd) {
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (clients[i].fd == fd) {
            clients[i].fd = -1;
            clients[i].user_id = 0;
            clients[i].room_id = 0;
            clients[i].buf_len = 0;
            clients[i].is_handshaked = 0;
            clients[i].last_operation_id = 0;
            if (clients[i].assembler) {
                fragment_assembler_free(clients[i].assembler);
                clients[i].assembler = NULL;
            }
            break;
        }
    }
}

static client_state_t* get_client(int fd) {
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (clients[i].fd == fd) {
            return &clients[i];
        }
    }
    return NULL;
}

// 房间状态管理函数
static room_state_t* get_room_state(uint32_t room_id) {
    pthread_mutex_lock(&rooms_mutex);
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (rooms[i] && rooms[i]->room_id == room_id) {
            pthread_mutex_unlock(&rooms_mutex);
            return rooms[i];
        }
    }
    pthread_mutex_unlock(&rooms_mutex);
    return NULL;
}

static room_state_t* create_room_state(uint32_t room_id) {
    room_state_t *room = malloc(sizeof(room_state_t));
    if (room) {
        room->room_id = room_id;
        room->next_operation_id = 1; // 操作ID从1开始
        room->canvas_data = strdup("[]"); // 初始空画布
        pthread_mutex_init(&room->mutex, NULL);
        
        // 添加到房间数组
        pthread_mutex_lock(&rooms_mutex);
        for (int i = 0; i < MAX_EVENTS; i++) {
            if (!rooms[i]) {
                rooms[i] = room;
                break;
            }
        }
        pthread_mutex_unlock(&rooms_mutex);
    }
    return room;
}

static void destroy_room_state(uint32_t room_id) {
    pthread_mutex_lock(&rooms_mutex);
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (rooms[i] && rooms[i]->room_id == room_id) {
            pthread_mutex_lock(&rooms[i]->mutex);
            if (rooms[i]->canvas_data) {
                free(rooms[i]->canvas_data);
            }
            pthread_mutex_unlock(&rooms[i]->mutex);
            pthread_mutex_destroy(&rooms[i]->mutex);
            free(rooms[i]);
            rooms[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&rooms_mutex);
}

static void update_room_canvas_data(uint32_t room_id, const char *data) {
    room_state_t *room = get_room_state(room_id);
    if (room) {
        pthread_mutex_lock(&room->mutex);
        if (room->canvas_data) {
            free(room->canvas_data);
        }
        room->canvas_data = strdup(data);
        pthread_mutex_unlock(&room->mutex);
    }
}

// Helper to send WebSocket Frame
static void send_ws_frame(int fd, const char *payload) {
    if (!payload) {
        LOG_ERROR("send_ws_frame: payload is NULL");
        return;
    }
    if (fd <= 0) {
        LOG_ERROR("send_ws_frame: invalid fd=%d", fd);
        return;
    }
    size_t len = strlen(payload);
    
    LOG_DEBUG("Preparing to send WS frame: fd=%d, payload_len=%zu", fd, len);
    
    // 动态分配足够大的帧缓冲区
    size_t max_frame_len = len + 14; // 最大头部14字节
    char *frame = malloc(max_frame_len);
    if (!frame) {
        LOG_ERROR("Failed to allocate frame buffer (%zu bytes)", max_frame_len);
        return;
    }
    
    // 构建帧
    size_t header_len = 2;
    frame[0] = 0x81; // FIN, Text frame
    
    if (len < 126) {
        frame[1] = len;
    } else if (len < 65536) {
        frame[1] = 126;
        frame[2] = (len >> 8) & 0xFF;
        frame[3] = len & 0xFF;
        header_len = 4;
    } else {
        frame[1] = 127;
        // 64位长度
        uint64_t len64 = len;
        for (int i = 0; i < 8; i++) {
            frame[2 + i] = (len64 >> (56 - i * 8)) & 0xFF;
        }
        header_len = 10;
    }
    
    // 复制数据
    memcpy(frame + header_len, payload, len);
    size_t total_len = header_len + len;
    
    LOG_DEBUG("Sending WS frame: total_len=%zu, header_len=%zu, payload_len=%zu", 
           total_len, header_len, len);
    
    // 分段发送大帧
    ssize_t total_sent = 0;
    int attempt = 0;
    while (total_sent < (ssize_t)total_len && attempt < 10) {
        attempt++;
        
        ssize_t sent = send(fd, frame + total_sent, total_len - total_sent, MSG_NOSIGNAL);
        
        if (sent <= 0) {
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                usleep(1000); // 等待1ms再试
                continue;
            }
            LOG_ERROR("Failed to send WS frame: %s", strerror(errno));
            break;
        }
        total_sent += sent;
        
        LOG_DEBUG("Sent %zd/%zu bytes of frame", total_sent, total_len);
    }
    
    if (total_sent != (ssize_t)total_len) {
        LOG_WARNING("Incomplete frame sent: %zd/%zu bytes", total_sent, total_len);
    } else {
        LOG_INFO("Sent WS frame: %zu bytes (payload: %zu)", total_len, len);
    }

    free(frame);
}

// Broadcast message to all clients in the same room (except sender)
static void broadcast_room(int sender_fd, uint32_t room_id, const char *msg, size_t len) {
    if (room_id == 0) return;
    
    // 计算正确的帧大小
    size_t header_len;
    if (len < 126) {
        header_len = 2;
    } else if (len < 65536) {
        header_len = 4;
    } else {
        header_len = 10;
    }
    
    size_t frame_len = len + header_len;
    char *frame = malloc(frame_len);
    if (!frame) return;
    
    if (ws_build_frame(msg, len, frame, &frame_len) != 0) {
        free(frame);
        return;
    }

    for (int i = 0; i < MAX_EVENTS; i++) {
        if (clients[i].fd != -1 && clients[i].fd != sender_fd && clients[i].room_id == room_id) {
            send(clients[i].fd, frame, frame_len, MSG_NOSIGNAL);
        }
    }
    free(frame);
}

// Send JSON response to a specific client
static void send_json_response(int fd, const char *type, const char *status, const char *msg) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddStringToObject(root, "status", status);
    if (msg) cJSON_AddStringToObject(root, "message", msg);
    char *json = cJSON_PrintUnformatted(root);
    
    send_ws_frame(fd, json);
    
    LOG_INFO("Sending response: type=%s, status=%s", type, status);

    cJSON_Delete(root);
    free(json);
}

// 广播任务处理函数
static void broadcast_task_handler(void *arg) {
    broadcast_task_t *task = (broadcast_task_t *)arg;
    if (!task) return;
    
    printf("DEBUG: broadcast_task_handler called - room_id=%u, msg_len=%zu, msg=%.50s...\n", 
           task->room_id, task->message_len, task->message);
    
    // 计算正确的帧大小
    size_t header_len;
    if (task->message_len < 126) {
        header_len = 2;
    } else if (task->message_len < 65536) {
        header_len = 4;
    } else {
        header_len = 10;
    }
    
    size_t frame_len = task->message_len + header_len;
    char *frame = malloc(frame_len);
    if (!frame) {
        printf("ERROR: Failed to allocate frame\n");
        free(task->message);
        free(task);
        return;
    }
    
    if (ws_build_frame(task->message, task->message_len, frame, &frame_len) != 0) {
        printf("ERROR: ws_build_frame failed\n");
        free(frame);
        free(task->message);
        free(task);
        return;
    }

    int send_count = 0;
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (clients[i].fd != -1 && clients[i].fd != task->client_fd && clients[i].room_id == task->room_id) {
            printf("DEBUG: Broadcasting to client fd=%d\n", clients[i].fd);
            send(clients[i].fd, frame, frame_len, MSG_NOSIGNAL);
            send_count++;
        }
    }
    printf("DEBUG: Broadcasted to %d clients\n", send_count);
    
    free(frame);
    free(task->message);
    free(task);
}

// 保存画布任务处理函数
static void save_canvas_task_handler(void *arg) {
    save_canvas_task_t *task = (save_canvas_task_t *)arg;
    if (!task) return;
    
    LOG_INFO("Saving canvas for room %u, data length: %zu", 
           task->room_id, strlen(task->canvas_data));
    
    // 验证是否为有效JSON
    cJSON *test_json = cJSON_Parse(task->canvas_data);
    if (!test_json) {
        LOG_ERROR("Canvas data is not valid JSON.");
        send_json_response(task->client_fd, "save_canvas_resp", "error", "Invalid JSON data");
        free(task->canvas_data);
        free(task);
        return;
    }
    cJSON_Delete(test_json);
    
    // 保存到数据库
    LOG_INFO("Calling db_save_project_data for room %u", task->room_id);
    int result = db_save_project_data(task->room_id, task->canvas_data);
    
    if (result == 0) {
        LOG_INFO("Canvas saved successfully for room %u", task->room_id);
        send_json_response(task->client_fd, "save_canvas_resp", "ok", "Canvas saved");
        
        // 验证保存是否成功
        char *saved_data = db_get_project_data(task->room_id);
        if (saved_data) {
            LOG_INFO("Verified save - retrieved data length: %zu", strlen(saved_data));
            free(saved_data);
        }
    } else {
        LOG_ERROR("Failed to save canvas for room %u. Database error.", task->room_id);
        send_json_response(task->client_fd, "save_canvas_resp", "error", "Database save failed");
    }
    
    free(task->canvas_data);
    free(task);
}

// 登录任务处理函数
static void login_task_handler(void *arg) {
    login_task_t *task = (login_task_t *)arg;
    if (!task) return;
    
    uint32_t uid = db_login_user(task->username, task->password);
    client_state_t *client = get_client(task->client_fd);
    
    if (uid > 0 && client) {
        client->user_id = uid;
        strncpy(client->username, task->username, sizeof(client->username)-1);
        send_json_response(task->client_fd, "login_resp", "ok", "Login successful");
        printf("User %s (ID: %u) logged in.\n", client->username, uid);
    } else {
        send_json_response(task->client_fd, "login_resp", "error", "Invalid credentials");
    }
    
    free(task->username);
    free(task->password);
    free(task);
}

// 注册任务处理函数
static void register_task_handler(void *arg) {
    register_task_t *task = (register_task_t *)arg;
    if (!task) return;
    
    uint32_t uid = db_register_user(task->username, task->password, task->email);
    if (uid > 0) {
        send_json_response(task->client_fd, "register_resp", "ok", "Registration successful");
        printf("User %s registered successfully (ID: %u)\n", task->username, uid);
    } else {
        send_json_response(task->client_fd, "register_resp", "error", "Username or Email already exists");
        printf("Registration failed for %s: Duplicate username/email\n", task->username);
    }
    
    free(task->username);
    free(task->password);
    free(task->email);
    free(task);
}

static void handle_message(client_state_t *client, char *msg, size_t len) {
    (void)len;
    printf("DEBUG: handle_message called with fd=%d, len=%zu\n", client->fd, len);
    cJSON *root = cJSON_Parse(msg);
    if (!root) {
        printf("DEBUG: Failed to parse JSON\n");
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) {
        printf("DEBUG: No type field in JSON\n");
        cJSON_Delete(root);
        return;
    }
    
    printf("DEBUG: Received message type: %s\n", type->valuestring);

    if (strcmp(type->valuestring, "login") == 0) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data) {
            cJSON *u = cJSON_GetObjectItem(data, "username");
            cJSON *p = cJSON_GetObjectItem(data, "password");
            if (u && p) {
                // 创建登录任务
                login_task_t *task = malloc(sizeof(login_task_t));
                if (task) {
                    task->client_fd = client->fd;
                    task->username = strdup(u->valuestring);
                    task->password = strdup(p->valuestring);
                    if (task->username && task->password) {
                        thread_pool_add_task(TASK_TYPE_DB, login_task_handler, task);
                    } else {
                        free(task->username);
                        free(task->password);
                        free(task);
                    }
                }
            }
        }
    } else if (strcmp(type->valuestring, "register") == 0) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data) {
            cJSON *u = cJSON_GetObjectItem(data, "username");
            cJSON *p = cJSON_GetObjectItem(data, "password");
            cJSON *e = cJSON_GetObjectItem(data, "email");
            if (u && p && e) {
                // 创建注册任务
                register_task_t *task = malloc(sizeof(register_task_t));
                if (task) {
                    task->client_fd = client->fd;
                    task->username = strdup(u->valuestring);
                    task->password = strdup(p->valuestring);
                    task->email = strdup(e->valuestring);
                    if (task->username && task->password && task->email) {
                        thread_pool_add_task(TASK_TYPE_DB, register_task_handler, task);
                    } else {
                        free(task->username);
                        free(task->password);
                        free(task->email);
                        free(task);
                    }
                }
            } else {
                send_json_response(client->fd, "register_resp", "error", "Missing required fields");
            }
        }
    } else if (strcmp(type->valuestring, "list_rooms") == 0) {
        if (client->user_id == 0) {
             send_json_response(client->fd, "error", "auth", "Not logged in");
        } else {
            int count = 0;
            project_t *projects = db_get_projects(client->user_id, &count);
            
            // Build response manually or use helper
            room_list_msg_t list_msg;
            list_msg.count = count;
            // Convert project_t to room_msg_t (compatible structures mostly)
            list_msg.rooms = malloc(sizeof(room_msg_t) * count);
            for(int i=0; i<count; i++) {
                list_msg.rooms[i].room_id = projects[i].id;
                list_msg.rooms[i].owner_id = projects[i].owner_id;
                strncpy(list_msg.rooms[i].name, projects[i].name, sizeof(list_msg.rooms[i].name)-1);
                strncpy(list_msg.rooms[i].description, projects[i].description, sizeof(list_msg.rooms[i].description)-1);
            }
            
            char *resp = protocol_serialize_room_list(&list_msg);
            if (resp) {
                send_ws_frame(client->fd, resp);
                free(resp);
            }
            
            free(list_msg.rooms);
            db_free_project_list(projects);
        }
    } else if (strcmp(type->valuestring, "create_room") == 0) {
        if (client->user_id == 0) return;
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data) {
            cJSON *name = cJSON_GetObjectItem(data, "name");
            cJSON *desc = cJSON_GetObjectItem(data, "description");
            if (name) {
                uint32_t pid = db_create_project(client->user_id, name->valuestring, desc ? desc->valuestring : "");
                if (pid > 0) {
                    send_json_response(client->fd, "create_room_resp", "ok", "Room created");
                } else {
                    send_json_response(client->fd, "create_room_resp", "error", "Failed to create room");
                }
            }
        }
    } else if (strcmp(type->valuestring, "join_room") == 0) {
        if (client->user_id == 0) return;
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data) {
            cJSON *rid = cJSON_GetObjectItem(data, "room_id");
            if (rid) {
                client->room_id = (uint32_t)rid->valuedouble;
                
                LOG_INFO("Client fd=%d joining room %u", client->fd, client->room_id);
                
                if (client->fd <= 0) {
                    LOG_WARNING("Client has invalid fd, skipping join_room");
                    return;
                }
                
                room_state_t *room = get_room_state(client->room_id);
                if (!room) {
                    room = create_room_state(client->room_id);
                }
                if (room) {
                    pthread_mutex_lock(&room->mutex);
                    client->last_operation_id = room->next_operation_id > 0 ? room->next_operation_id - 1 : 0;
                    LOG_INFO("Client fd=%d last_operation_id set to %lu", client->fd, client->last_operation_id);
                    pthread_mutex_unlock(&room->mutex);
                }
                
                send_json_response(client->fd, "join_room_resp", "ok", "Joined room");
                printf("User %s (ID: %u) joined room %u\n", client->username, client->user_id, client->room_id);
                
                LOG_INFO("Broadcasting user_joined to room %u", client->room_id);
                cJSON *join_notif = cJSON_CreateObject();
                cJSON_AddStringToObject(join_notif, "type", "user_joined");
                cJSON_AddStringToObject(join_notif, "username", client->username);
                cJSON_AddNumberToObject(join_notif, "user_id", client->user_id);
                char *join_json = cJSON_PrintUnformatted(join_notif);
                
                int broadcast_count = 0;
                for (int i = 0; i < MAX_EVENTS; i++) {
                    if (clients[i].fd != -1 && clients[i].fd != client->fd && 
                        clients[i].room_id == client->room_id && clients[i].is_handshaked) {
                        LOG_INFO("Sending user_joined to client fd=%d", clients[i].fd);
                        send_ws_frame(clients[i].fd, join_json);
                        broadcast_count++;
                    }
                }
                LOG_INFO("Broadcasted user_joined to %d clients", broadcast_count);
                
                free(join_json);
                cJSON_Delete(join_notif);
                
                LOG_INFO("Loading canvas data for room %u", client->room_id);
                char *canvas_data = db_get_project_data(client->room_id);
                LOG_INFO("db_get_project_data returned: %s", canvas_data ? "data" : "NULL");
                
                if (canvas_data) {
                    LOG_INFO("Canvas data pointer: %p", (void*)canvas_data);
                    
                    size_t canvas_len = 0;
                    const char *p = canvas_data;
                    while (*p && canvas_len < 100000) {
                        canvas_len++;
                        p++;
                    }
                    LOG_INFO("Canvas data length: %zu, first 100 chars: %.100s%s", 
                           canvas_len, canvas_data, 
                           canvas_len > 100 ? "..." : "");
                    
                    if (canvas_len > 0) {
                        LOG_INFO("Creating JSON object for load_canvas (data len: %zu)...", canvas_len);
                        cJSON *root = cJSON_CreateObject();
                        if (!root) {
                            LOG_ERROR("Failed to create JSON object for load_canvas");
                            free(canvas_data);
                            canvas_data = NULL;
                        } else {
                            LOG_INFO("Adding type field...");
                            cJSON_AddStringToObject(root, "type", "load_canvas");
                            LOG_INFO("Adding data field...");
                            cJSON_AddStringToObject(root, "data", canvas_data);
                            LOG_INFO("Printing JSON (this may take a while for large data)...");
                            char *json = cJSON_PrintUnformatted(root);
                            if (json) {
                                size_t json_len = 0;
                                const char *jp = json;
                                while (*jp && json_len < 1000000) {
                                    json_len++;
                                    jp++;
                                }
                                LOG_INFO("Sending load_canvas to client, JSON length: %zu", json_len);
                                send_ws_frame(client->fd, json);
                                free(json);
                            } else {
                                LOG_ERROR("Failed to print JSON for load_canvas");
                            }
                            LOG_INFO("Deleting JSON root...");
                            cJSON_Delete(root);
                            LOG_INFO("JSON root deleted");
                        }
                    }
                }
                if (canvas_data) free(canvas_data);
                
                int chat_count = 0;
                chat_msg_db_t *chat_history = db_get_chat_messages(client->room_id, &chat_count, 50);
                if (chat_history && chat_count > 0) {
                    cJSON *chat_root = cJSON_CreateObject();
                    if (chat_root) {
                        cJSON_AddStringToObject(chat_root, "type", "chat_history");
                        cJSON *messages = cJSON_CreateArray();
                        if (messages) {
                            for (int i = 0; i < chat_count; i++) {
                                cJSON *msg = cJSON_CreateObject();
                                if (msg) {
                                    cJSON_AddStringToObject(msg, "sender", chat_history[i].sender);
                                    cJSON_AddStringToObject(msg, "content", chat_history[i].content);
                                    cJSON_AddNumberToObject(msg, "timestamp", (double)chat_history[i].timestamp);
                                    cJSON_AddItemToArray(messages, msg);
                                }
                            }
                            cJSON_AddItemToObject(chat_root, "messages", messages);
                        }
                        char *chat_json = cJSON_PrintUnformatted(chat_root);
                        if (chat_json) {
                            send_ws_frame(client->fd, chat_json);
                            free(chat_json);
                        }
                        cJSON_Delete(chat_root);
                    }
                    free(chat_history);
                }
            }
        }
    } else if (strcmp(type->valuestring, "save_canvas") == 0) {
        if (client->room_id == 0) {
            LOG_ERROR("Cannot save, client not in any room.");
            return;
        }
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data && cJSON_IsString(data)) {
            // 创建保存画布任务
            save_canvas_task_t *task = malloc(sizeof(save_canvas_task_t));
            if (task) {
                task->client_fd = client->fd;
                task->room_id = client->room_id;
                task->canvas_data = strdup(data->valuestring);
                if (task->canvas_data) {
                    thread_pool_add_task(TASK_TYPE_DB, save_canvas_task_handler, task);
                } else {
                    free(task);
                }
            }
        } else {
            LOG_ERROR("Save canvas request missing or invalid 'data' field.");
            send_json_response(client->fd, "save_canvas_resp", "error", "Missing or invalid data");
        }
    } else if (strcmp(type->valuestring, "chat") == 0) {
        if (client->room_id > 0) {
            room_state_t *room = get_room_state(client->room_id);
            if (!room) {
                room = create_room_state(client->room_id);
            }
            
            if (room) {
                uint64_t operation_id = 0;
                pthread_mutex_lock(&room->mutex);
                operation_id = room->next_operation_id++;
                pthread_mutex_unlock(&room->mutex);
                
                cJSON *root_with_id = cJSON_Parse(msg);
                if (root_with_id) {
                    cJSON *data = cJSON_GetObjectItem(root_with_id, "data");
                    uint64_t msg_timestamp = (uint64_t)time(NULL);
                    const char *msg_content = "";
                    
                    if (data) {
                        cJSON_DeleteItemFromObject(data, "sender");
                        cJSON_AddStringToObject(data, "sender", client->username);
                        
                        cJSON *content = cJSON_GetObjectItem(data, "content");
                        if (content && content->valuestring) {
                            msg_content = content->valuestring;
                        }
                        
                        cJSON *ts = cJSON_GetObjectItem(data, "timestamp");
                        if (!ts || ts->valuedouble == 0) {
                            cJSON_DeleteItemFromObject(data, "timestamp");
                            cJSON_AddNumberToObject(data, "timestamp", (double)msg_timestamp);
                        } else {
                            msg_timestamp = (uint64_t)ts->valuedouble;
                        }
                    }
                    cJSON_AddNumberToObject(root_with_id, "operation_id", operation_id);
                    
                    db_save_chat_message(client->room_id, client->username, msg_content, msg_timestamp);
                    
                    char *msg_with_id = cJSON_PrintUnformatted(root_with_id);
                    
                    broadcast_task_t *task = malloc(sizeof(broadcast_task_t));
                    if (task && msg_with_id) {
                        task->client_fd = client->fd;
                        task->room_id = client->room_id;
                        task->message = msg_with_id;
                        task->message_len = strlen(msg_with_id);
                        task->operation_id = operation_id;
                        thread_pool_add_task(TASK_TYPE_BROADCAST, broadcast_task_handler, task);
                    } else {
                        if (msg_with_id) free(msg_with_id);
                        if (task) free(task);
                    }
                    cJSON_Delete(root_with_id);
                }
            }
        }
    } else if (strcmp(type->valuestring, "draw") == 0 || 
               strcmp(type->valuestring, "undo") == 0 ||
               strcmp(type->valuestring, "redo") == 0 ||
               strcmp(type->valuestring, "clear") == 0) {
        
        if (client->room_id > 0) {
            room_state_t *room = get_room_state(client->room_id);
            if (!room) {
                room = create_room_state(client->room_id);
            }
            
            if (room) {
                uint64_t operation_id = 0;
                pthread_mutex_lock(&room->mutex);
                operation_id = room->next_operation_id++;
                pthread_mutex_unlock(&room->mutex);
                
                cJSON *root_with_id = cJSON_Parse(msg);
                if (root_with_id) {
                    cJSON_AddNumberToObject(root_with_id, "operation_id", operation_id);
                    char *msg_with_id = cJSON_PrintUnformatted(root_with_id);
                    
                    broadcast_task_t *task = malloc(sizeof(broadcast_task_t));
                    if (task && msg_with_id) {
                        task->client_fd = client->fd;
                        task->room_id = client->room_id;
                        task->message = msg_with_id;
                        task->message_len = strlen(msg_with_id);
                        task->operation_id = operation_id;
                        thread_pool_add_task(TASK_TYPE_BROADCAST, broadcast_task_handler, task);
                    } else {
                        if (msg_with_id) free(msg_with_id);
                        if (task) free(task);
                    }
                    cJSON_Delete(root_with_id);
                }
            }
        }
    }

    cJSON_Delete(root);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    
    // 初始化日志系统
    logger_init(LOG_LEVEL_INFO, "server.log");
    LOG_INFO("Starting CanvasHub server");
    
    // 初始化线程池
    if (thread_pool_init() != 0) {
        LOG_FATAL("Failed to initialize thread pool. Exiting.");
        return EXIT_FAILURE;
    }
    
    // Init DB
    // Use environment variables or fallback to defaults
    const char *db_pass = getenv("DB_PASS");
    db_config_t db_conf = {"localhost", 3306, "root", "", "canvas_db"};
    strncpy(db_conf.password, db_pass ? db_pass : "MTsql001.", sizeof(db_conf.password)-1);
    
    if (db_init(&db_conf) != 0) {
        LOG_WARNING("Database init failed. Auth features may not work.");
        // Don't exit, just continue without DB features (or with limited features)
        // But if DB is required for everything, we should probably exit or retry.
        // For now, let's keep running but mark DB as unavailable if we had a flag.
    } else {
        LOG_INFO("Database initialized.");
    }

    init_clients();

    int server_fd, epoll_fd;
    struct sockaddr_in address;
    struct epoll_event ev, events[MAX_EVENTS];
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
    
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1 failed");
        exit(EXIT_FAILURE);
    }
    
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl: server_fd");
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d\n", PORT);

    while(1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            break;
        }
        
        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.fd == server_fd) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
                if (client_fd == -1) {
                    perror("accept");
                    continue;
                }
                
                set_nonblocking(client_fd);
                ev.events = EPOLLIN;
                ev.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
                    perror("epoll_ctl: client_fd");
                    close(client_fd);
                    continue;
                }
                
                if (add_client(client_fd) == -1) {
                    printf("Too many clients, closing connection\n");
                    close(client_fd);
                } else {
                    printf("New connection: %d\n", client_fd);
                }
            } else {
                int fd = events[n].data.fd;
                client_state_t *client = get_client(fd);
                // CRITICAL FIX: If get_client returns NULL, we must NOT continue processing
                if (!client) {
                    // This might happen if fd was closed but event still arrived?
                    // Or if our client list is out of sync.
                    // Ideally remove from epoll just in case.
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    continue;
                }

                ssize_t count = read(fd, client->buffer + client->buf_len, sizeof(client->buffer) - client->buf_len);
                
                if (count == -1) {
                    if (errno != EAGAIN) {
                        perror("read");
                        remove_client(fd);
                        close(fd);
                    }
                } else if (count == 0) {
                    remove_client(fd);
                    close(fd);
                    printf("Client disconnected: %d\n", fd);
                } else {
                client->buf_len += count;
                
                // 检查缓冲区是否快满
                if (client->buf_len >= (int)sizeof(client->buffer) - 1) {
                    printf("WARNING: Buffer near full for client %d, buf_len=%d\n", fd, client->buf_len);
                }
                
                // Process buffer
                while (client->buf_len > 0) {
                    if (client->is_handshaked) {
                         // WebSocket Frame Processing
                         unsigned char *buf = (unsigned char *)client->buffer;
                         if (client->buf_len < 2) break; // Need at least 2 bytes for header
                         
                         int payload_start = 2;
                         int payload_len = buf[1] & 0x7F;
                         
                         if (payload_len == 126) {
    if (client->buf_len < 4) break;
    payload_start = 4;
    payload_len = (buf[2] << 8) | buf[3];
} else if (payload_len == 127) {
    if (client->buf_len < 10) break;
    payload_start = 10;
    // 64位长度
    payload_len = 0;
    for (int i = 2; i < 10; i++) {
        payload_len = (payload_len << 8) | buf[i];
    }
}

int mask_len = (buf[1] & 0x80) ? 4 : 0;
int total_frame_len = payload_start + mask_len + payload_len;

// 检查帧是否超过缓冲区大小
if (total_frame_len > (int)sizeof(client->buffer)) {
    printf("ERROR: Frame too large (%d bytes), max buffer size: %zu. Disconnecting client %d\n", 
           total_frame_len, sizeof(client->buffer), fd);
    remove_client(fd);
    close(fd);
    break;
}

if (client->buf_len < total_frame_len) {
    // Wait for more data
    break;
}
                         
                         // We have a full frame
                         if (mask_len > 0) {
                             unsigned char mask[4];
                             memcpy(mask, buf + payload_start, 4);
                             payload_start += 4;
                             for(int i=0; i<payload_len; i++) {
                                 buf[payload_start+i] ^= mask[i%4];
                             }
                         }
                          
                         // 检查是否是大帧分片
                         large_frame_header_t header;
                         char *fragment_data = NULL;
                         size_t fragment_size = 0;
                         int parse_result = large_frame_parse((const char *)(buf + payload_start), payload_len, &header, &fragment_data, &fragment_size);
                         
                         if (parse_result == 0) {
                             // 是大帧分片
                             LOG_DEBUG("Received large frame fragment: message_id=%u, fragment=%u/%u, size=%u", 
                                    header.message_id, header.fragment_index, header.total_fragments, header.fragment_size);
                             
                             // 初始化或获取组装器
                             if (!client->assembler || client->assembler->message_id != header.message_id) {
                                 if (client->assembler) {
                                     fragment_assembler_free(client->assembler);
                                 }
                                 client->assembler = fragment_assembler_init(header.message_id, header.total_fragments, header.total_size);
                             }
                             
                             if (client->assembler) {
                                 // 添加分片
                                 fragment_assembler_add(client->assembler, header.fragment_index, fragment_data, fragment_size);
                                 
                                 // 检查是否组装完成
                                 if (fragment_assembler_is_complete(client->assembler)) {
                                     size_t total_size = 0;
                                     char *complete_data = fragment_assembler_get_data(client->assembler, &total_size);
                                     if (complete_data) {
                                         LOG_INFO("Large frame assembled: total size=%zu bytes", total_size);
                                         // 处理完整的数据
                                         handle_message(client, complete_data, total_size);
                                         // 注意：complete_data 由 assembler 管理，不需要手动释放
                                     }
                                     // 释放组装器
                                     fragment_assembler_free(client->assembler);
                                     client->assembler = NULL;
                                 }
                             }
                         } else {
                             // 普通 WebSocket 消息
                             // Create null-terminated copy for safe processing
                             char *msg = malloc(payload_len + 1);
                             if (msg) {
                                 memcpy(msg, buf + payload_start, payload_len);
                                 msg[payload_len] = '\0';
                                 printf("Received WS Frame (%d bytes): %.100s%s\n", payload_len, msg, payload_len > 100 ? "..." : "");
                                 handle_message(client, msg, payload_len);
                                 free(msg);
                             }
                         }
                          
                         // Move remaining data to front
                         int remaining = client->buf_len - total_frame_len;
                         if (remaining > 0) {
                             memmove(client->buffer, client->buffer + total_frame_len, remaining);
                         }
                         client->buf_len = remaining;
                         
                    } else {
                        // Handshake
                        if (ws_is_handshake(client->buffer)) {
                             // ... handshake logic ...
                             // Assumes handshake fits in one read, which is usually true.
                             // Simplified for demo.
                             char key[256];
                             if (ws_parse_handshake_key(client->buffer, key, sizeof(key)) == 0) {
                                 char response[512];
                                 size_t resp_len = sizeof(response);
                                 if (ws_generate_handshake_response(key, response, &resp_len) == 0) {
                                     write(fd, response, resp_len);
                                     client->is_handshaked = 1;
                                     printf("Handshake successful with %d\n", fd);
                                 }
                             }
                             // Clear buffer after handshake
                             client->buf_len = 0; 
                        } else {
                            // Invalid protocol or waiting for full handshake
                            if (client->buf_len == sizeof(client->buffer)) {
                                // Buffer full and no handshake? Kill it.
                                remove_client(fd);
                                close(fd);
                            }
                            break; 
                        }
                    }
                }
                }
            }
        }
    }
    
    db_close();
    close(server_fd);
    
    // 关闭线程池
    thread_pool_shutdown();
    
    // 关闭日志系统
    logger_close();
    
    return 0;
}
