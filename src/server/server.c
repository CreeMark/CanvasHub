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
#include "protocol.h"
#include "protocol_ws.h"
#include "database.h"
#include "cJSON.h"

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
} client_state_t;

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

// Helper to send WebSocket Frame
static void send_ws_frame(int fd, const char *payload) {
    if (!payload || fd <= 0) return;
    size_t len = strlen(payload);
    
    g_print("DEBUG: Preparing to send WS frame: payload_len=%zu\n", len);
    
    // 动态分配足够大的帧缓冲区
    size_t max_frame_len = len + 14; // 最大头部14字节
    char *frame = malloc(max_frame_len);
    if (!frame) {
        g_print("ERROR: Failed to allocate frame buffer (%zu bytes)\n", max_frame_len);
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
    
    g_print("DEBUG: Sending WS frame: total_len=%zu, header_len=%zu, payload_len=%zu\n", 
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
            g_print("ERROR: Failed to send WS frame: %s\n", strerror(errno));
            break;
        }
        total_sent += sent;
        
        g_print("DEBUG: Sent %zd/%zu bytes of frame\n", total_sent, total_len);
    }
    
    if (total_sent != (ssize_t)total_len) {
        g_print("WARNING: Incomplete frame sent: %zd/%zu bytes\n", total_sent, total_len);
    } else {
        g_print("INFO: Sent WS frame: %zu bytes (payload: %zu)\n", total_len, len);
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
    
    g_print("Sending response: type=%s, status=%s\n", type, status);

    cJSON_Delete(root);
    free(json);
}

static void handle_message(client_state_t *client, char *msg, size_t len) {
    (void)len;
    cJSON *root = cJSON_Parse(msg);
    if (!root) return;

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type->valuestring, "login") == 0) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data) {
            cJSON *u = cJSON_GetObjectItem(data, "username");
            cJSON *p = cJSON_GetObjectItem(data, "password");
            if (u && p) {
                uint32_t uid = db_login_user(u->valuestring, p->valuestring);
                if (uid > 0) {
                    client->user_id = uid;
                    strncpy(client->username, u->valuestring, sizeof(client->username)-1);
                    send_json_response(client->fd, "login_resp", "ok", "Login successful");
                    printf("User %s (ID: %u) logged in.\n", client->username, uid);
                } else {
                    send_json_response(client->fd, "login_resp", "error", "Invalid credentials");
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
                uint32_t uid = db_register_user(u->valuestring, p->valuestring, e->valuestring);
                if (uid > 0) {
                    send_json_response(client->fd, "register_resp", "ok", "Registration successful");
                    printf("User %s registered successfully (ID: %u)\n", u->valuestring, uid);
                } else {
                    send_json_response(client->fd, "register_resp", "error", "Username or Email already exists");
                    printf("Registration failed for %s: Duplicate username/email\n", u->valuestring);
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
                send_json_response(client->fd, "join_room_resp", "ok", "Joined room");
                printf("User %u joined room %u\n", client->user_id, client->room_id);
                
                // Send current canvas state
                char *canvas_data = db_get_project_data(client->room_id);
                if (canvas_data && strlen(canvas_data) > 0) {
                    cJSON *root = cJSON_CreateObject();
                    cJSON_AddStringToObject(root, "type", "load_canvas");
                    cJSON_AddStringToObject(root, "data", canvas_data);
                    char *json = cJSON_PrintUnformatted(root);
                    send_ws_frame(client->fd, json);
                    free(json);
                    cJSON_Delete(root);
                }
                if (canvas_data) free(canvas_data);
            }
        }
    } else if (strcmp(type->valuestring, "save_canvas") == 0) {
         if (client->room_id == 0) {
        g_print("ERROR: Cannot save, client not in any room.\n");
        return;
    }
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (data && cJSON_IsString(data)) {
        size_t data_len = strlen(data->valuestring);
        g_print("INFO: Saving canvas for room %u, data length: %zu\n", 
               client->room_id, data_len);
        
        // 验证是否为有效JSON
        cJSON *test_json = cJSON_Parse(data->valuestring);
        if (!test_json) {
            g_print("ERROR: Canvas data is not valid JSON.\n");
            send_json_response(client->fd, "save_canvas_resp", "error", "Invalid JSON data");
            return;
        }
        cJSON_Delete(test_json);
        
        // 保存到数据库
        g_print("INFO: Calling db_save_project_data for room %u\n", client->room_id);
        int result = db_save_project_data(client->room_id, data->valuestring);
        
        if (result == 0) {
            g_print("INFO: Canvas saved successfully for room %u\n", client->room_id);
            send_json_response(client->fd, "save_canvas_resp", "ok", "Canvas saved");
            
            // 验证保存是否成功
            char *saved_data = db_get_project_data(client->room_id);
            if (saved_data) {
                g_print("INFO: Verified save - retrieved data length: %zu\n", strlen(saved_data));
                free(saved_data);
            }
        } else {
            g_print("ERROR: Failed to save canvas for room %u. Database error.\n", client->room_id);
            send_json_response(client->fd, "save_canvas_resp", "error", "Database save failed");
        }
    } else {
        g_print("ERROR: Save canvas request missing or invalid 'data' field.\n");
        send_json_response(client->fd, "save_canvas_resp", "error", "Missing or invalid data");
    }} else if (strcmp(type->valuestring, "draw") == 0 || 
               strcmp(type->valuestring, "chat") == 0 ||
               strcmp(type->valuestring, "undo") == 0 ||
               strcmp(type->valuestring, "redo") == 0 ||
               strcmp(type->valuestring, "clear") == 0) {
        
        if (client->room_id > 0) {
            broadcast_room(client->fd, client->room_id, msg, len);
        }
    }

    cJSON_Delete(root);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    
    // Init DB
    // Use environment variables or fallback to defaults
    const char *db_pass = getenv("DB_PASS");
    db_config_t db_conf = {"localhost", 3306, "root", "", "canvas_db"};
    strncpy(db_conf.password, db_pass ? db_pass : "MTsql001.", sizeof(db_conf.password)-1);
    
    if (db_init(&db_conf) != 0) {
        fprintf(stderr, "Warning: Database init failed. Auth features may not work.\n");
        // Don't exit, just continue without DB features (or with limited features)
        // But if DB is required for everything, we should probably exit or retry.
        // For now, let's keep running but mark DB as unavailable if we had a flag.
    } else {
        printf("Database initialized.\n");
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
                         
                         // Create null-terminated copy for safe processing
                         char *msg = malloc(payload_len + 1);
                         if (msg) {
                             memcpy(msg, buf + payload_start, payload_len);
                             msg[payload_len] = '\0';
                             printf("Received WS Frame (%d bytes): %.100s%s\n", payload_len, msg, payload_len > 100 ? "..." : "");
                             handle_message(client, msg, payload_len);
                             free(msg);
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
    return 0;
}
