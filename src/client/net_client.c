#include <gtk/gtk.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "network.h"
#include "protocol_ws.h"

#define BUFFER_SIZE 16384

typedef struct {
    int expecting_big_frame;
    size_t expected_frame_size;
} client_extra_state_t;


struct net_client_s {
    int fd;
    char server_ip[64];  // 服务器 IP 地址
    int server_port;     // 服务器端口
    char buffer[BUFFER_SIZE];
    size_t buf_len;
    int connected;
    int handshake_complete;
    
    net_callback_t on_message;
    net_callback_t on_connect;
    net_callback_t on_disconnect;
    void *user_data;
    
    guint io_watch_id;
    client_extra_state_t extra; 
};

static gboolean socket_io_callback(GIOChannel *source, GIOCondition condition, gpointer data);

net_client_t *net_client_new(const char *server_ip, int port) {
    net_client_t *client = calloc(1, sizeof(net_client_t));
    if (!client) return NULL;
    
    client->fd = -1;
    
    // 设置服务器地址和端口
    if (server_ip) {
        strncpy(client->server_ip, server_ip, sizeof(client->server_ip) - 1);
    } else {
        strcpy(client->server_ip, "127.0.0.1");  // 默认本地地址
    }
    client->server_port = port > 0 ? port : 8080;  // 默认端口 8080
    
    g_print("INFO: Client initialized, will connect to %s:%d\n", 
           client->server_ip, client->server_port);
    
    return client;
}

void net_client_set_callbacks(net_client_t *client, 
                              net_callback_t on_connect,
                              net_callback_t on_disconnect,
                              net_callback_t on_message,
                              void *user_data) {
    if (client) {
        client->on_connect = on_connect;
        client->on_disconnect = on_disconnect;
        client->on_message = on_message;
        client->user_data = user_data;
    }
}

int net_client_connect(net_client_t *client) {
    if (!client) return -1;
    
    client->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client->fd == -1) return -1;
    
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(client->server_port);
    
    g_print("INFO: Connecting to %s:%d...\n", client->server_ip, client->server_port);
    
    if (inet_pton(AF_INET, client->server_ip, &serv_addr.sin_addr) <= 0) {
        g_print("ERROR: Invalid server address: %s\n", client->server_ip);
        close(client->fd);
        return -1;
    }
    
    if (connect(client->fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        g_print("ERROR: Connection failed: %s\n", strerror(errno));
        close(client->fd);
        return -1;
    }
    
    g_print("INFO: Connected to server successfully\n");
    
    // Send WebSocket Handshake
    char handshake[512];
    snprintf(handshake, sizeof(handshake),
        "GET / HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        client->server_ip, client->server_port);
        
    send(client->fd, handshake, strlen(handshake), 0);
    
    // Set non-blocking for GLib main loop integration
    int flags = fcntl(client->fd, F_GETFL, 0);
    fcntl(client->fd, F_SETFL, flags | O_NONBLOCK);
    
    // Add to GTK main loop
    GIOChannel *channel = g_io_channel_unix_new(client->fd);
    client->io_watch_id = g_io_add_watch(channel, G_IO_IN | G_IO_HUP | G_IO_ERR, 
                                         socket_io_callback, client);
    g_io_channel_unref(channel);
    
    return 0;
}

static gboolean socket_io_callback(GIOChannel *source, GIOCondition condition, gpointer data) {
    (void)source;
    net_client_t *client = (net_client_t *)data;
    
    if (condition & (G_IO_HUP | G_IO_ERR)) {
        if (client->connected) {
            client->connected = 0;
            if (client->on_disconnect) client->on_disconnect(NULL, 0, client->user_data);
        }
        close(client->fd);
        client->fd = -1;
        return FALSE; // Remove source
    }
    
    if (condition & G_IO_IN) {
        // 计算剩余空间
        size_t available_space = sizeof(client->buffer) - client->buf_len;
        
        if (available_space <= 0) {
            g_print("ERROR: Buffer overflow! Current buffer length: %zu, max: %zu\n", 
                   client->buf_len, sizeof(client->buffer));
            
            // 尝试解析已接收的帧
            if (client->handshake_complete && client->buf_len > 0) {
                char *payload = NULL;
                size_t payload_len = 0;
                int consumed = ws_parse_frame(client->buffer, client->buf_len, &payload, &payload_len);
                
                if (consumed > 0) {
                    // 处理完整帧
                    if (client->on_message && payload) {
                        char *safe_payload = malloc(payload_len + 1);
                        if (safe_payload) {
                            memcpy(safe_payload, payload, payload_len);
                            safe_payload[payload_len] = '\0';
                            client->on_message(safe_payload, payload_len, client->user_data);
                            free(safe_payload);
                        }
                    }
                    if (payload) free(payload);
                    
                    // 移动剩余数据
                    size_t remaining = client->buf_len - consumed;
                    if (remaining > 0) {
                        memmove(client->buffer, client->buffer + consumed, remaining);
                    }
                    client->buf_len = remaining;
                    
                    // 重置available_space计算
                    available_space = sizeof(client->buffer) - client->buf_len;
                } else {
                    // 如果没有完整帧，清空缓冲区
                    g_print("ERROR: No complete frame in full buffer, clearing\n");
                    client->buf_len = 0;
                    available_space = sizeof(client->buffer);
                }
            } else {
                // 握手未完成或无数据，清空缓冲区
                client->buf_len = 0;
                available_space = sizeof(client->buffer);
            }
        }
        
        if (available_space > 0) {
            ssize_t n = recv(client->fd, client->buffer + client->buf_len, available_space, 0);
            
            if (n > 0) {
                client->buf_len += n;
                
                if (!client->handshake_complete) {
                    char *end_of_header = strstr(client->buffer, "\r\n\r\n");
                    if (end_of_header) {
                        client->handshake_complete = 1;
                        client->connected = 1;
                        
                        size_t header_len = (end_of_header - client->buffer) + 4;
                        size_t remaining = client->buf_len - header_len;
                        if (remaining > 0) {
                            memmove(client->buffer, client->buffer + header_len, remaining);
                        }
                        client->buf_len = remaining;
                        
                        if (client->on_connect) client->on_connect(NULL, 0, client->user_data);
                    }
                }
                
                if (client->handshake_complete && client->buf_len > 0) {
                    int processed = 0;
                    while (client->buf_len > 0 && processed < 10) {
                        processed++;
                        
                        char *payload = NULL;
                        size_t payload_len = 0;
                        int consumed = ws_parse_frame(client->buffer, client->buf_len, &payload, &payload_len);
                        
                        if (consumed == -2) {
                            g_print("DEBUG: Incomplete frame: buffer has %zu bytes, waiting for more\n", client->buf_len);
                            break;
                        } else if (consumed == -1) {
                            g_print("ERROR: Frame parsing error, clearing buffer\n");
                            client->buf_len = 0;
                            break;
                        } else if (consumed > 0) {
                            g_print("INFO: Parsed frame: consumed=%d, payload_len=%zu\n", consumed, payload_len);
                            
                            if (client->on_message && payload) {
                                char *safe_payload = malloc(payload_len + 1);
                                if (safe_payload) {
                                    memcpy(safe_payload, payload, payload_len);
                                    safe_payload[payload_len] = '\0';
                                    client->on_message(safe_payload, payload_len, client->user_data);
                                    free(safe_payload);
                                }
                            }
                            
                            if (payload) free(payload);
                            
                            size_t remaining = client->buf_len - consumed;
                            if (remaining > 0) {
                                memmove(client->buffer, client->buffer + consumed, remaining);
                            }
                            client->buf_len = remaining;
                        } else {
                            break;
                        }
                    }
                }
            } else if (n == 0) {
                g_print("INFO: Connection closed by peer\n");
                if (client->connected) {
                    client->connected = 0;
                    if (client->on_disconnect) client->on_disconnect(NULL, 0, client->user_data);
                }
                close(client->fd);
                client->fd = -1;
                return FALSE;
            } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                g_print("ERROR: recv error: %s\n", strerror(errno));
                if (client->connected) {
                    client->connected = 0;
                    if (client->on_disconnect) client->on_disconnect(NULL, 0, client->user_data);
                }
                close(client->fd);
                client->fd = -1;
                return FALSE;
            }
        }
    }
    
    return TRUE;
}

int net_client_send(net_client_t *client, const void *data, size_t len) {
    if (!client || client->fd == -1 || !client->connected) 
    {
        g_print("ERROR: 无法发送, 客户端未连接\n");
        return -1;
    }
    
    // 检查数据大小
    if (len > 1024 * 1024) {
        g_print("ERROR: 数据大小超过发送限制 (%zu bytes)\n", len);
        return -1;
    }

    // 动态分配缓冲区：header(最大10字节) + mask(4字节) + payload
    size_t max_frame_len = len + 14;
    char *frame = malloc(max_frame_len);
    if (!frame) {
        g_print("ERROR: Failed to allocate frame buffer (%zu bytes)\n", max_frame_len);
        return -1;
    }

    frame[0] = 0x81; // FIN, Text
    size_t header_len = 2;
    
    // 根据RFC 6455，客户端必须设置掩码位
    if (len < 126) {
        frame[1] = 0x80 | len; // Mask bit SET
    } else if (len < 65536) {
        frame[1] = 0x80 | 126; // Mask bit SET
        frame[2] = (len >> 8) & 0xFF;
        frame[3] = len & 0xFF;
        header_len = 4;
    } else {
        frame[1] = 0x80 | 127; // Mask bit SET
        uint64_t len64 = len;
        for (int i = 7; i >= 0; i--) {
            frame[2 + (7 - i)] = (len64 >> (i * 8)) & 0xFF;
        }
        header_len = 10;
    }
    
    // 生成随机掩码（RFC 6455要求）
    unsigned char mask[4];
    srand((unsigned int)time(NULL) ^ (unsigned int)len);
    for (int i = 0; i < 4; i++) {
        mask[i] = rand() & 0xFF;
    }
    
    // 写入掩码
    memcpy(frame + header_len, mask, 4);
    header_len += 4;
    
    // 复制并掩码处理数据
    const unsigned char *src = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++) {
        frame[header_len + i] = src[i] ^ mask[i % 4];
    }
    
    size_t total_len = header_len + len;
    ssize_t sent = send(client->fd, frame, total_len, 0);
    
    free(frame);
    
    if (sent != (ssize_t)total_len) {
        g_print("WARNING: Incomplete send: %zd/%zu bytes\n", sent, total_len);
        return -1;
    }
    
    g_print("INFO: Sent %zu bytes (payload: %zu, header+mask: %zu)\n", 
           total_len, len, header_len);
    
    return sent;
}

void net_client_service(net_client_t *client) {
    // Not needed with GTK main loop integration
    (void)client;
}

void net_client_free(net_client_t *client) {
    if (client) {
        if (client->fd != -1) {
            g_source_remove(client->io_watch_id);
            close(client->fd);
        }
        free(client);
    }
}
